#include "engine/pipeline/block_builders/source_block_builder.hpp"
#include "engine/pipeline/builders/source_builder.hpp"
#include "engine/pipeline/builders/queue_builder.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::block_builders {

SourceBlockBuilder::SourceBlockBuilder(GstElement* bin,
                                       std::unordered_map<std::string, GstElement*>& tails)
    : bin_(bin), tails_(tails) {}

bool SourceBlockBuilder::build(const engine::core::config::PipelineConfig& config) {
    // Build nvmultiurisrcbin
    builders::SourceBuilder src_builder(bin_);
    GstElement* source = src_builder.build(config, 0);
    if (!source) {
        LOG_E("SourceBlockBuilder: failed to build source element");
        return false;
    }

    GstElement* tail = source;

    // Optional output queue
    if (config.sources.output_queue.has_value()) {
        builders::QueueBuilder q_builder(bin_);
        GstElement* q = q_builder.build(config.sources.output_queue.value(), "src_output_queue");
        if (!q) {
            LOG_E("SourceBlockBuilder: failed to build output queue");
            return false;
        }
        // Note: nvmultiurisrcbin uses dynamic pads (pad-added signal).
        // Linking to the output queue is handled via the pad-added callback,
        // not a static link here. The queue is added to the bin for later
        // dynamic connection.
        tail = q;
    }

    tails_["src"] = tail;
    LOG_I("SourceBlockBuilder: phase 1 complete (tail='{}')", GST_ELEMENT_NAME(tail));
    return true;
}

}  // namespace engine::pipeline::block_builders
