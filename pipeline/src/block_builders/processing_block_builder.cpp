#include "engine/pipeline/block_builders/processing_block_builder.hpp"
#include "engine/pipeline/builders/infer_builder.hpp"
#include "engine/pipeline/builders/tracker_builder.hpp"
#include "engine/pipeline/builders/analytics_builder.hpp"
#include "engine/pipeline/builders/queue_builder.hpp"
#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::block_builders {

// ── Helper: expose a ghost pad on a bin pointing at target_elem's pad ───────────────
// Returns true on success. Does NOT take ownership of target_elem.
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

ProcessingBlockBuilder::ProcessingBlockBuilder(GstElement* pipeline,
                                               std::unordered_map<std::string, GstElement*>& tails)
    : pipeline_(pipeline), tails_(tails) {}

bool ProcessingBlockBuilder::build(const engine::core::config::PipelineConfig& config) {
    const auto& proc = config.processing;
    if (proc.elements.empty()) {
        LOG_W("ProcessingBlockBuilder: no processing elements configured");
        tails_["proc"] = tails_["src"];
        return true;
    }

    // ── Create processing_bin ──────────────────────────────────────────────────
    GstElement* proc_bin = gst_bin_new("processing_bin");
    if (!proc_bin) {
        LOG_E("ProcessingBlockBuilder: failed to create processing_bin");
        return false;
    }

    GstElement* first_elem = nullptr;  // ← ghost sink target
    GstElement* prev = nullptr;        // running link tail inside proc_bin
    builders::QueueBuilder q_builder(proc_bin);

    for (int i = 0; i < static_cast<int>(proc.elements.size()); ++i) {
        const auto& elem_cfg = proc.elements[i];

        // Insert queue BEFORE element (if queue: {} configured on this element)
        if (elem_cfg.has_queue) {
            std::string q_name = elem_cfg.id + "_queue";
            GstElement* q = q_builder.build(elem_cfg.queue, q_name);
            if (!q) {
                LOG_E("ProcessingBlockBuilder: failed to build queue for '{}'", elem_cfg.id);
                gst_object_unref(proc_bin);
                return false;
            }
            if (!first_elem)
                first_elem = q;
            if (prev && !gst_element_link(prev, q)) {
                LOG_E("ProcessingBlockBuilder: link failed {} → {}", GST_ELEMENT_NAME(prev),
                      q_name);
                gst_object_unref(proc_bin);
                return false;
            }
            prev = q;
        }

        // Build the processing element
        GstElement* elem = nullptr;
        if (elem_cfg.type == "nvinfer") {
            builders::InferBuilder builder(proc_bin);
            elem = builder.build(config, i);
        } else if (elem_cfg.type == "nvtracker") {
            builders::TrackerBuilder builder(proc_bin);
            elem = builder.build(config, i);
        } else if (elem_cfg.type == "nvdsanalytics") {
            builders::AnalyticsBuilder builder(proc_bin);
            elem = builder.build(config, i);
        } else {
            LOG_E("ProcessingBlockBuilder: unknown type '{}'", elem_cfg.type);
            gst_object_unref(proc_bin);
            return false;
        }

        if (!elem) {
            LOG_E("ProcessingBlockBuilder: failed to build '{}'", elem_cfg.id);
            gst_object_unref(proc_bin);
            return false;
        }
        if (!first_elem)
            first_elem = elem;
        if (prev && !gst_element_link(prev, elem)) {
            LOG_E("ProcessingBlockBuilder: link failed {} → {}", GST_ELEMENT_NAME(prev),
                  GST_ELEMENT_NAME(elem));
            gst_object_unref(proc_bin);
            return false;
        }
        prev = elem;
    }

    if (!first_elem || !prev) {
        LOG_E("ProcessingBlockBuilder: empty element chain");
        gst_object_unref(proc_bin);
        return false;
    }

    // ── Expose ghost sink (first element) + ghost src (last element) ─────────────────
    if (!expose_ghost(proc_bin, first_elem, "sink", "sink") ||
        !expose_ghost(proc_bin, prev, "src", "src")) {
        gst_object_unref(proc_bin);
        return false;
    }

    // ── Add proc_bin to pipeline, link sources_bin → processing_bin ────────────────
    if (!gst_bin_add(GST_BIN(pipeline_), proc_bin)) {
        LOG_E("ProcessingBlockBuilder: failed to add processing_bin to pipeline");
        gst_object_unref(proc_bin);
        return false;
    }
    if (!gst_element_link(tails_["src"], proc_bin)) {
        LOG_E("ProcessingBlockBuilder: failed to link sources_bin → processing_bin");
        return false;
    }

    tails_["proc"] = proc_bin;
    LOG_I("ProcessingBlockBuilder: phase 2 complete (processing_bin, last='{}')",
          GST_ELEMENT_NAME(prev));
    return true;
}

}  // namespace engine::pipeline::block_builders
