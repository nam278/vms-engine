#pragma once
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>
#include <unordered_map>
#include <string>

namespace engine::pipeline::block_builders {

/**
 * @brief Phase 1 — Build source block (sources_bin wrapping nvmultiurisrcbin).
 *
 * Creates a GstBin named "sources_bin", adds nvmultiurisrcbin to it,
 * exposes a ghost src pad, and adds the bin to the top-level pipeline.
 * No queue is inserted — the first element of the next block provides
 * the inter-block thread boundary via its own queue: {} entry.
 * Updates the tails map with "src" → sources_bin.
 */
class SourceBlockBuilder {
   public:
    /**
     * @param pipeline Top-level GstPipeline that owns all block bins.
     * @param tails    Shared tail map updated to "src" → sources_bin.
     */
    SourceBlockBuilder(GstElement* pipeline, std::unordered_map<std::string, GstElement*>& tails);

    /**
     * @brief Build the source block.
     * @param config Full pipeline config.
     * @return true if built successfully.
     */
    bool build(const engine::core::config::PipelineConfig& config);

   private:
    GstElement* pipeline_ = nullptr;
    std::unordered_map<std::string, GstElement*>& tails_;
};

}  // namespace engine::pipeline::block_builders
