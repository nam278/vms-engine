#include "engine/pipeline/block_builders/outputs_block_builder.hpp"
#include "engine/pipeline/builders/encoder_builder.hpp"
#include "engine/pipeline/builders/sink_builder.hpp"
#include "engine/pipeline/builders/msgconv_builder.hpp"
#include "engine/pipeline/builders/msgbroker_builder.hpp"
#include "engine/pipeline/builders/demuxer_builder.hpp"
#include "engine/pipeline/builders/queue_builder.hpp"
#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::block_builders {

namespace {

void apply_gpu_id_if_supported(GstElement* element, int gpu_id) {
    if (element == nullptr) {
        return;
    }

    GParamSpec* gpu_id_prop = g_object_class_find_property(G_OBJECT_GET_CLASS(element), "gpu-id");
    if (gpu_id_prop != nullptr) {
        g_object_set(G_OBJECT(element), "gpu-id", static_cast<guint>(gpu_id), nullptr);
    }
}

bool apply_generic_output_properties(GstElement* element,
                                     const engine::core::config::OutputElementConfig& elem_cfg) {
    if (element == nullptr) {
        return false;
    }

    apply_gpu_id_if_supported(element, elem_cfg.gpu_id);

    if (elem_cfg.type == "capsfilter" && !elem_cfg.caps.empty()) {
        GstCaps* caps = gst_caps_from_string(elem_cfg.caps.c_str());
        if (caps == nullptr) {
            LOG_E("OutputsBlockBuilder: invalid caps '{}' for '{}'", elem_cfg.caps, elem_cfg.id);
            return false;
        }
        g_object_set(G_OBJECT(element), "caps", caps, nullptr);
        gst_caps_unref(caps);
    }

    if (elem_cfg.type == "nvvideoconvert") {
        if (!elem_cfg.nvbuf_memory_type.empty()) {
            gst_util_set_object_arg(G_OBJECT(element), "nvbuf-memory-type",
                                    elem_cfg.nvbuf_memory_type.c_str());
        }
        if (!elem_cfg.src_crop.empty()) {
            g_object_set(G_OBJECT(element), "src-crop", elem_cfg.src_crop.c_str(), nullptr);
        }
        if (!elem_cfg.dest_crop.empty()) {
            g_object_set(G_OBJECT(element), "dest-crop", elem_cfg.dest_crop.c_str(), nullptr);
        }
    }

    return true;
}

}  // namespace

// ── Helper: expose a ghost pad on a bin pointing at target_elem's pad ───────────────────────────
static bool expose_ghost(GstElement* bin, GstElement* target_elem, const char* pad_name,
                         const char* ghost_name) {
    engine::core::utils::GstPadPtr pad(gst_element_get_static_pad(target_elem, pad_name),
                                       gst_object_unref);
    if (!pad) {
        LOG_E("expose_ghost: no '{}' pad on '{}'", pad_name, GST_ELEMENT_NAME(target_elem));
        return false;
    }
    GstPad* ghost = gst_ghost_pad_new(ghost_name, pad.get());
    gst_pad_set_active(ghost, TRUE);
    gst_element_add_pad(bin, ghost);
    return true;
}

OutputsBlockBuilder::OutputsBlockBuilder(GstElement* pipeline,
                                         std::unordered_map<std::string, GstElement*>& tails)
    : pipeline_(pipeline), tails_(tails) {}

bool OutputsBlockBuilder::build(const engine::core::config::PipelineConfig& config) {
    if (config.outputs.empty()) {
        LOG_W("OutputsBlockBuilder: no outputs configured");
        return true;
    }

    // Determine upstream input bin — prefer "vis" if visuals are enabled
    GstElement* upstream = nullptr;
    if (tails_.count("vis")) {
        upstream = tails_["vis"];
    } else if (tails_.count("proc")) {
        upstream = tails_["proc"];
    } else {
        LOG_E("OutputsBlockBuilder: no input tail available");
        return false;
    }

    // For multiple outputs, insert a tee (in pipeline_ directly)
    GstElement* tee = nullptr;
    if (config.outputs.size() > 1) {
        auto tee_guard = engine::core::utils::make_gst_element("tee", "output_tee");
        if (!tee_guard) {
            LOG_E("OutputsBlockBuilder: failed to create tee");
            return false;
        }
        if (!gst_bin_add(GST_BIN(pipeline_), tee_guard.get())) {
            LOG_E("OutputsBlockBuilder: failed to add tee to pipeline");
            return false;
        }
        tee = tee_guard.release();

        if (!gst_element_link(upstream, tee)) {
            LOG_E("OutputsBlockBuilder: failed to link upstream → tee");
            return false;
        }
    }

    for (int i = 0; i < static_cast<int>(config.outputs.size()); ++i) {
        // When tee is present, insert a per-branch queue in pipeline_ before the output_bin
        GstElement* branch_input = tee ? nullptr : upstream;
        if (tee) {
            builders::QueueBuilder q_builder(pipeline_);
            engine::core::config::QueueConfig default_q;
            std::string q_name = config.outputs[i].id + "_tee_queue";
            GstElement* q = q_builder.build(default_q, q_name);
            if (!q) {
                LOG_E("OutputsBlockBuilder: failed to build tee queue for '{}'",
                      config.outputs[i].id);
                return false;
            }
            if (!gst_element_link(tee, q)) {
                LOG_E("OutputsBlockBuilder: failed to link tee → {}", q_name);
                return false;
            }
            branch_input = q;
        }

        // Build per-output GstBin and link
        GstElement* out_bin = build_output_bin(config, i);
        if (!out_bin) {
            LOG_E("OutputsBlockBuilder: failed to build output_bin for '{}'", config.outputs[i].id);
            return false;
        }
        if (!gst_element_link(branch_input, out_bin)) {
            LOG_E("OutputsBlockBuilder: failed to link upstream → output_bin_{}",
                  config.outputs[i].id);
            return false;
        }
    }

    LOG_I("OutputsBlockBuilder: phase 4 complete ({} outputs)", config.outputs.size());
    return true;
}

GstElement* OutputsBlockBuilder::build_output_bin(
    const engine::core::config::PipelineConfig& config, int output_index) {
    const auto& output_cfg = config.outputs[output_index];

    // ── Create per-output GstBin ─────────────────────────────────────────────────────────────────
    std::string bin_name = "output_bin_" + output_cfg.id;
    GstElement* out_bin = gst_bin_new(bin_name.c_str());
    if (!out_bin) {
        LOG_E("OutputsBlockBuilder: failed to create '{}'", bin_name);
        return nullptr;
    }

    GstElement* first_elem = nullptr;  // ← ghost sink target
    GstElement* prev = nullptr;        // running link tail inside out_bin
    builders::QueueBuilder q_builder(out_bin);

    for (int j = 0; j < static_cast<int>(output_cfg.elements.size()); ++j) {
        const auto& elem_cfg = output_cfg.elements[j];

        // Insert queue BEFORE element (if queue: {} configured on this element)
        if (elem_cfg.has_queue) {
            std::string q_name = elem_cfg.id + "_queue";
            GstElement* q = q_builder.build(elem_cfg.queue, q_name);
            if (!q) {
                LOG_E("OutputsBlockBuilder: failed to build queue for '{}'", elem_cfg.id);
                gst_object_unref(out_bin);
                return nullptr;
            }
            if (!first_elem)
                first_elem = q;
            if (prev && !gst_element_link(prev, q)) {
                LOG_E("OutputsBlockBuilder: link failed {} → {}", GST_ELEMENT_NAME(prev), q_name);
                gst_object_unref(out_bin);
                return nullptr;
            }
            prev = q;
        }

        GstElement* elem = nullptr;

        if (elem_cfg.type == "nvv4l2h264enc" || elem_cfg.type == "nvv4l2h265enc") {
            builders::EncoderBuilder builder(out_bin);
            elem = builder.build(config, output_index * 100 + j);
        } else if (elem_cfg.type == "rtspclientsink" || elem_cfg.type == "fakesink" ||
                   elem_cfg.type == "filesink") {
            builders::SinkBuilder builder(out_bin);
            elem = builder.build(config, output_index * 100 + j);
        } else if (elem_cfg.type == "nvmsgconv") {
            builders::MsgconvBuilder builder(out_bin);
            elem = builder.build(config, output_index * 100 + j);
        } else if (elem_cfg.type == "nvmsgbroker") {
            builders::MsgbrokerBuilder builder(out_bin);
            elem = builder.build(config, output_index * 100 + j);
        } else {
            // Generic element — try factory make
            auto guard =
                engine::core::utils::make_gst_element(elem_cfg.type.c_str(), elem_cfg.id.c_str());
            if (!guard) {
                LOG_E("OutputsBlockBuilder: unknown element type '{}' for '{}'", elem_cfg.type,
                      elem_cfg.id);
                gst_object_unref(out_bin);
                return nullptr;
            }
            if (!apply_generic_output_properties(guard.get(), elem_cfg)) {
                gst_object_unref(out_bin);
                return nullptr;
            }
            if (!gst_bin_add(GST_BIN(out_bin), guard.get())) {
                LOG_E("OutputsBlockBuilder: failed to add '{}' to {}", elem_cfg.id, bin_name);
                gst_object_unref(out_bin);
                return nullptr;
            }
            elem = guard.release();
        }

        if (!elem) {
            LOG_E("OutputsBlockBuilder: failed to build '{}' (type={})", elem_cfg.id,
                  elem_cfg.type);
            gst_object_unref(out_bin);
            return nullptr;
        }
        if (!first_elem)
            first_elem = elem;
        if (prev && !gst_element_link(prev, elem)) {
            LOG_E("OutputsBlockBuilder: link failed {} → {}", GST_ELEMENT_NAME(prev),
                  GST_ELEMENT_NAME(elem));
            gst_object_unref(out_bin);
            return nullptr;
        }
        prev = elem;
    }

    if (!first_elem) {
        LOG_E("OutputsBlockBuilder: empty element chain in '{}'", bin_name);
        gst_object_unref(out_bin);
        return nullptr;
    }

    // ── Expose ghost sink (entry queue or first element) ─────────────────────────────────────────
    if (!expose_ghost(out_bin, first_elem, "sink", "sink")) {
        gst_object_unref(out_bin);
        return nullptr;
    }

    // ── Add out_bin to pipeline
    // ───────────────────────────────────────────────────────────────────
    if (!gst_bin_add(GST_BIN(pipeline_), out_bin)) {
        LOG_E("OutputsBlockBuilder: failed to add '{}' to pipeline", bin_name);
        gst_object_unref(out_bin);
        return nullptr;
    }

    LOG_I("OutputsBlockBuilder: built '{}' (first='{}', last='{}')", bin_name,
          GST_ELEMENT_NAME(first_elem), GST_ELEMENT_NAME(prev));
    return out_bin;  // pipeline_ owns it
}

}  // namespace engine::pipeline::block_builders
