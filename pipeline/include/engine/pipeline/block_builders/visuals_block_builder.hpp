#pragma once
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>
#include <unordered_map>
#include <string>

namespace engine::pipeline::block_builders {

/**
 * @brief Phase 3 — Build visuals chain (tiler + OSD).
 *
 * Creates tiler and OSD elements with optional inline queues.
 * Skipped entirely if config.visuals.enable == false.
 * Updates tails map with "vis" key.
 */
class VisualsBlockBuilder {
   public:
    VisualsBlockBuilder(GstElement* bin, std::unordered_map<std::string, GstElement*>& tails);

    bool build(const engine::core::config::PipelineConfig& config);

   private:
    GstElement* bin_ = nullptr;
    std::unordered_map<std::string, GstElement*>& tails_;
};

}  // namespace engine::pipeline::block_builders
