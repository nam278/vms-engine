#pragma once
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>
#include <unordered_map>
#include <string>

namespace engine::pipeline::block_builders {

/**
 * @brief Phase 2 — Build processing chain (nvinfer, nvtracker, nvdsanalytics).
 *
 * Creates and links inference elements with optional inline queues.
 * Updates tails map with "proc" key.
 */
class ProcessingBlockBuilder {
   public:
    ProcessingBlockBuilder(GstElement* bin, std::unordered_map<std::string, GstElement*>& tails);

    bool build(const engine::core::config::PipelineConfig& config);

   private:
    GstElement* bin_ = nullptr;
    std::unordered_map<std::string, GstElement*>& tails_;
};

}  // namespace engine::pipeline::block_builders
