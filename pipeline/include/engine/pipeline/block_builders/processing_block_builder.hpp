#pragma once
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>
#include <unordered_map>
#include <string>

namespace engine::pipeline::block_builders {

/**
 * @brief Phase 2 — Build processing block (processing_bin wrapping inference chain).
 *
 * Creates a GstBin named "processing_bin", builds all processing elements
 * inside it with optional per-element queues (queue BEFORE the element),
 * exposes ghost sink + src pads, links sources_bin → processing_bin.
 * Updates tails map with "proc" → processing_bin.
 */
class ProcessingBlockBuilder {
   public:
    /**
     * @param pipeline Top-level GstPipeline that owns all block bins.
     * @param tails    Shared tail map; reads "src", writes "proc" → processing_bin.
     */
    ProcessingBlockBuilder(GstElement* pipeline,
                           std::unordered_map<std::string, GstElement*>& tails);

    bool build(const engine::core::config::PipelineConfig& config);

   private:
    GstElement* pipeline_ = nullptr;
    std::unordered_map<std::string, GstElement*>& tails_;
};

}  // namespace engine::pipeline::block_builders
