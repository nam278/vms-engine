#pragma once
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>
#include <unordered_map>
#include <string>

namespace engine::pipeline::block_builders {

/**
 * @brief Phase 4 — Build output blocks (one GstBin per output).
 *
 * For each OutputConfig, creates a GstBin named "output_bin_{id}", builds
 * the element chain (encoder, capsfilter, sink, etc.) inside it, exposes a
 * ghost sink pad, adds the bin to the top-level pipeline, then links the
 * upstream vis/proc bin → output_bin_{id}.
 * For multiple outputs a tee is inserted before branching.
 */
class OutputsBlockBuilder {
   public:
    /**
     * @param pipeline Top-level GstPipeline that owns all block bins.
     * @param tails    Shared tail map; reads "vis" (falls back to "proc").
     */
    OutputsBlockBuilder(GstElement* pipeline, std::unordered_map<std::string, GstElement*>& tails);

    bool build(const engine::core::config::PipelineConfig& config);

   private:
    GstElement* pipeline_ = nullptr;
    std::unordered_map<std::string, GstElement*>& tails_;

    /** @brief Build one output chain inside output_bin; returns the bin or nullptr. */
    GstElement* build_output_bin(const engine::core::config::PipelineConfig& config,
                                 int output_index);
};

}  // namespace engine::pipeline::block_builders
