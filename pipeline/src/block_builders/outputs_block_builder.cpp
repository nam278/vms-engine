#include "engine/pipeline/block_builders/outputs_block_builder.hpp"
#include "engine/pipeline/builders/encoder_builder.hpp"
#include "engine/pipeline/builders/sink_builder.hpp"
#include "engine/pipeline/builders/msgconv_builder.hpp"
#include "engine/pipeline/builders/msgbroker_builder.hpp"
#include "engine/pipeline/builders/demuxer_builder.hpp"
#include "engine/pipeline/builders/queue_builder.hpp"
#include "engine/core/utils/logger.hpp"
#include "engine/core/utils/gst_utils.hpp"

namespace engine::pipeline::block_builders {

OutputsBlockBuilder::OutputsBlockBuilder(GstElement* bin,
                                         std::unordered_map<std::string, GstElement*>& tails)
    : bin_(bin), tails_(tails) {}

bool OutputsBlockBuilder::build(const engine::core::config::PipelineConfig& config) {
    if (config.outputs.empty()) {
        LOG_W("OutputsBlockBuilder: no outputs configured");
        return true;
    }

    // Determine input point — prefer "vis" if visuals are enabled
    GstElement* input = nullptr;
    if (tails_.count("vis")) {
        input = tails_["vis"];
    } else if (tails_.count("proc")) {
        input = tails_["proc"];
    } else {
        LOG_E("OutputsBlockBuilder: no input tail available");
        return false;
    }

    // For multiple outputs, use a tee element
    GstElement* tee = nullptr;
    if (config.outputs.size() > 1) {
        auto tee_guard = engine::core::utils::make_gst_element("tee", "output_tee");
        if (!tee_guard) {
            LOG_E("OutputsBlockBuilder: failed to create tee");
            return false;
        }
        if (!gst_bin_add(GST_BIN(bin_), tee_guard.get())) {
            LOG_E("OutputsBlockBuilder: failed to add tee to bin");
            return false;
        }
        tee = tee_guard.release();

        if (!gst_element_link(input, tee)) {
            LOG_E("OutputsBlockBuilder: failed to link input → tee");
            return false;
        }
        input = tee;
    }

    for (int i = 0; i < static_cast<int>(config.outputs.size()); ++i) {
        GstElement* branch_input = input;

        // If using tee, create a queue for each branch
        if (tee) {
            builders::QueueBuilder q_builder(bin_);
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

        if (!build_output(config, i, branch_input)) {
            LOG_E("OutputsBlockBuilder: failed to build output '{}'", config.outputs[i].id);
            return false;
        }
    }

    LOG_I("OutputsBlockBuilder: phase 4 complete ({} outputs)", config.outputs.size());
    return true;
}

bool OutputsBlockBuilder::build_output(const engine::core::config::PipelineConfig& config,
                                       int output_index, GstElement* input) {
    const auto& output_cfg = config.outputs[output_index];
    GstElement* prev = input;
    builders::QueueBuilder q_builder(bin_);

    for (int j = 0; j < static_cast<int>(output_cfg.elements.size()); ++j) {
        const auto& elem_cfg = output_cfg.elements[j];

        // Insert inline queue if configured
        if (elem_cfg.has_queue) {
            std::string q_name = elem_cfg.id + "_queue";
            GstElement* q = q_builder.build(elem_cfg.queue, q_name);
            if (!q) {
                LOG_E("OutputsBlockBuilder: failed to build queue for '{}'", elem_cfg.id);
                return false;
            }
            if (!gst_element_link(prev, q)) {
                LOG_E("OutputsBlockBuilder: link failed: {} → {}", GST_ELEMENT_NAME(prev), q_name);
                return false;
            }
            prev = q;
        }

        GstElement* elem = nullptr;

        if (elem_cfg.type == "nvv4l2h264enc" || elem_cfg.type == "nvv4l2h265enc") {
            builders::EncoderBuilder builder(bin_);
            elem = builder.build(config, output_index * 100 + j);
        } else if (elem_cfg.type == "rtspclientsink" || elem_cfg.type == "fakesink" ||
                   elem_cfg.type == "filesink") {
            builders::SinkBuilder builder(bin_);
            elem = builder.build(config, output_index * 100 + j);
        } else if (elem_cfg.type == "nvmsgconv") {
            builders::MsgconvBuilder builder(bin_);
            elem = builder.build(config, output_index * 100 + j);
        } else if (elem_cfg.type == "nvmsgbroker") {
            builders::MsgbrokerBuilder builder(bin_);
            elem = builder.build(config, output_index * 100 + j);
        } else {
            // Generic element — try factory make
            auto guard =
                engine::core::utils::make_gst_element(elem_cfg.type.c_str(), elem_cfg.id.c_str());
            if (!guard) {
                LOG_E("OutputsBlockBuilder: unknown element type '{}'", elem_cfg.type);
                return false;
            }
            if (!gst_bin_add(GST_BIN(bin_), guard.get())) {
                return false;
            }
            elem = guard.release();
        }

        if (!elem) {
            LOG_E("OutputsBlockBuilder: failed to build '{}' (type={})", elem_cfg.id,
                  elem_cfg.type);
            return false;
        }

        if (!gst_element_link(prev, elem)) {
            LOG_E("OutputsBlockBuilder: link failed: {} → {}", GST_ELEMENT_NAME(prev),
                  GST_ELEMENT_NAME(elem));
            return false;
        }
        prev = elem;
    }

    return true;
}

}  // namespace engine::pipeline::block_builders
