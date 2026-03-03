#pragma once
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>
#include <unordered_map>
#include <string>

namespace engine::pipeline::block_builders {

/**
 * @brief Phase 4 — Build output chains (encoder + sink per output).
 *
 * For each OutputConfig, creates a chain of elements (encoder, capsfilter,
 * sink, msgconv, msgbroker, etc.) and links them.
 * Reads from tails["proc"] or tails["vis"] depending on visuals enabled.
 */
class OutputsBlockBuilder {
   public:
    OutputsBlockBuilder(GstElement* bin, std::unordered_map<std::string, GstElement*>& tails);

    bool build(const engine::core::config::PipelineConfig& config);

   private:
    GstElement* bin_ = nullptr;
    std::unordered_map<std::string, GstElement*>& tails_;

    bool build_output(const engine::core::config::PipelineConfig& config, int output_index,
                      GstElement* input);
};

}  // namespace engine::pipeline::block_builders
