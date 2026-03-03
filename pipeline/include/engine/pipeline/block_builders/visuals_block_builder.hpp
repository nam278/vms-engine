#pragma once
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>
#include <unordered_map>
#include <string>

namespace engine::pipeline::block_builders {

/**
 * @brief Phase 3 — Build visuals block (visuals_bin wrapping tiler + OSD).
 *
 * Creates a GstBin named "visuals_bin", builds tiler/osd elements inside it
 * with optional per-element queues (queue BEFORE the element), exposes ghost
 * sink + src pads, links processing_bin → visuals_bin.
 * Skipped entirely if config.visuals.enable == false.
 * Updates tails map with "vis" → visuals_bin (or passes through "proc").
 */
class VisualsBlockBuilder {
   public:
    /**
     * @param pipeline Top-level GstPipeline that owns all block bins.
     * @param tails    Shared tail map; reads "proc", writes "vis" → visuals_bin.
     */
    VisualsBlockBuilder(GstElement* pipeline, std::unordered_map<std::string, GstElement*>& tails);

    bool build(const engine::core::config::PipelineConfig& config);

   private:
    GstElement* pipeline_ = nullptr;
    std::unordered_map<std::string, GstElement*>& tails_;
};

}  // namespace engine::pipeline::block_builders
