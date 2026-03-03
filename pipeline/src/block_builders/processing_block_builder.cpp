#include "engine/pipeline/block_builders/processing_block_builder.hpp"
#include "engine/pipeline/builders/infer_builder.hpp"
#include "engine/pipeline/builders/tracker_builder.hpp"
#include "engine/pipeline/builders/analytics_builder.hpp"
#include "engine/pipeline/builders/queue_builder.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::block_builders {

ProcessingBlockBuilder::ProcessingBlockBuilder(GstElement* bin,
                                               std::unordered_map<std::string, GstElement*>& tails)
    : bin_(bin), tails_(tails) {}

bool ProcessingBlockBuilder::build(const engine::core::config::PipelineConfig& config) {
    const auto& proc = config.processing;
    if (proc.elements.empty()) {
        LOG_W("ProcessingBlockBuilder: no processing elements configured");
        tails_["proc"] = tails_["src"];
        return true;
    }

    GstElement* prev = tails_["src"];
    builders::QueueBuilder q_builder(bin_);

    for (int i = 0; i < static_cast<int>(proc.elements.size()); ++i) {
        const auto& elem_cfg = proc.elements[i];

        // Insert inline queue if configured
        if (elem_cfg.has_queue) {
            std::string q_name = elem_cfg.id + "_queue";
            GstElement* q = q_builder.build(elem_cfg.queue, q_name);
            if (!q) {
                LOG_E("ProcessingBlockBuilder: failed to build queue for '{}'", elem_cfg.id);
                return false;
            }
            if (!gst_element_link(prev, q)) {
                LOG_E("ProcessingBlockBuilder: failed to link {} → {}", GST_ELEMENT_NAME(prev),
                      q_name);
                return false;
            }
            prev = q;
        }

        // Build the processing element based on type
        GstElement* elem = nullptr;

        if (elem_cfg.type == "nvinfer") {
            builders::InferBuilder builder(bin_);
            elem = builder.build(config, i);
        } else if (elem_cfg.type == "nvtracker") {
            builders::TrackerBuilder builder(bin_);
            elem = builder.build(config, i);
        } else if (elem_cfg.type == "nvdsanalytics") {
            builders::AnalyticsBuilder builder(bin_);
            elem = builder.build(config, i);
        } else {
            LOG_E("ProcessingBlockBuilder: unknown type '{}'", elem_cfg.type);
            return false;
        }

        if (!elem) {
            LOG_E("ProcessingBlockBuilder: failed to build '{}'", elem_cfg.id);
            return false;
        }

        if (!gst_element_link(prev, elem)) {
            LOG_E("ProcessingBlockBuilder: failed to link {} → {}", GST_ELEMENT_NAME(prev),
                  GST_ELEMENT_NAME(elem));
            return false;
        }
        prev = elem;
    }

    // Optional output queue
    if (proc.output_queue.has_value()) {
        GstElement* oq = q_builder.build(proc.output_queue.value(), "proc_output_queue");
        if (!oq) {
            LOG_E("ProcessingBlockBuilder: failed to build output queue");
            return false;
        }
        if (!gst_element_link(prev, oq)) {
            LOG_E("ProcessingBlockBuilder: failed to link to output queue");
            return false;
        }
        prev = oq;
    }

    tails_["proc"] = prev;
    LOG_I("ProcessingBlockBuilder: phase 2 complete (tail='{}')", GST_ELEMENT_NAME(prev));
    return true;
}

}  // namespace engine::pipeline::block_builders
