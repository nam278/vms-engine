#pragma once
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>
#include <unordered_map>
#include <string>

namespace engine::pipeline::block_builders {

/**
 * @brief Phase 1 — Build source block (nvmultiurisrcbin → output_queue).
 *
 * Creates the source bin element and optional output queue.
 * Updates the tails map with "src" key.
 */
class SourceBlockBuilder {
   public:
    /**
     * @param bin   GstBin (the pipeline).
     * @param tails Shared tail map updated to "src" → tail element.
     */
    SourceBlockBuilder(GstElement* bin, std::unordered_map<std::string, GstElement*>& tails);

    /**
     * @brief Build the source block.
     * @param config Full pipeline config.
     * @return true if built and linked successfully.
     */
    bool build(const engine::core::config::PipelineConfig& config);

   private:
    GstElement* bin_ = nullptr;
    std::unordered_map<std::string, GstElement*>& tails_;
};

}  // namespace engine::pipeline::block_builders
