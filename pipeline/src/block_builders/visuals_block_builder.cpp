#include "engine/pipeline/block_builders/visuals_block_builder.hpp"
#include "engine/pipeline/builders/tiler_builder.hpp"
#include "engine/pipeline/builders/osd_builder.hpp"
#include "engine/pipeline/builders/queue_builder.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::block_builders {

VisualsBlockBuilder::VisualsBlockBuilder(GstElement* bin,
                                         std::unordered_map<std::string, GstElement*>& tails)
    : bin_(bin), tails_(tails) {}

bool VisualsBlockBuilder::build(const engine::core::config::PipelineConfig& config) {
    if (!config.visuals.enable) {
        LOG_I("VisualsBlockBuilder: visuals disabled, skipping");
        tails_["vis"] = tails_["proc"];
        return true;
    }

    const auto& vis = config.visuals;
    GstElement* prev = tails_["proc"];
    builders::QueueBuilder q_builder(bin_);

    for (int i = 0; i < static_cast<int>(vis.elements.size()); ++i) {
        const auto& elem_cfg = vis.elements[i];

        // Insert inline queue if configured
        if (elem_cfg.has_queue) {
            std::string q_name = elem_cfg.id + "_queue";
            GstElement* q = q_builder.build(elem_cfg.queue, q_name);
            if (!q) {
                LOG_E("VisualsBlockBuilder: failed to build queue for '{}'", elem_cfg.id);
                return false;
            }
            if (!gst_element_link(prev, q)) {
                LOG_E("VisualsBlockBuilder: failed to link {} → {}", GST_ELEMENT_NAME(prev),
                      q_name);
                return false;
            }
            prev = q;
        }

        GstElement* elem = nullptr;

        if (elem_cfg.type == "nvmultistreamtiler") {
            builders::TilerBuilder builder(bin_);
            elem = builder.build(config, i);
        } else if (elem_cfg.type == "nvdsosd") {
            builders::OsdBuilder builder(bin_);
            elem = builder.build(config, i);
        } else {
            LOG_E("VisualsBlockBuilder: unknown type '{}'", elem_cfg.type);
            return false;
        }

        if (!elem) {
            LOG_E("VisualsBlockBuilder: failed to build '{}'", elem_cfg.id);
            return false;
        }

        if (!gst_element_link(prev, elem)) {
            LOG_E("VisualsBlockBuilder: failed to link {} → {}", GST_ELEMENT_NAME(prev),
                  GST_ELEMENT_NAME(elem));
            return false;
        }
        prev = elem;
    }

    // Optional output queue
    if (vis.output_queue.has_value()) {
        GstElement* oq = q_builder.build(vis.output_queue.value(), "vis_output_queue");
        if (!oq) {
            LOG_E("VisualsBlockBuilder: failed to build output queue");
            return false;
        }
        if (!gst_element_link(prev, oq)) {
            LOG_E("VisualsBlockBuilder: failed to link to output queue");
            return false;
        }
        prev = oq;
    }

    tails_["vis"] = prev;
    LOG_I("VisualsBlockBuilder: phase 3 complete (tail='{}')", GST_ELEMENT_NAME(prev));
    return true;
}

}  // namespace engine::pipeline::block_builders
