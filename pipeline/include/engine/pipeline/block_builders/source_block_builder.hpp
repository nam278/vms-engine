#pragma once
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>
#include <unordered_map>
#include <string>

namespace engine::pipeline::block_builders {

/**
 * @brief Phase 1 — Build source block.
 *
 * Creates a single outer GstBin named "sources_bin" and then chooses one of
 * two source architectures from `config.sources.type`:
 * - `nvmultiurisrcbin`: legacy DeepStream-managed multi-source path
 * - `nvurisrcbin`: manual `nvurisrcbin_N -> nvstreammux` path
 *
 * In manual mode, the mux and all source bins are attached directly under
 * `sources_bin`; there is no second nested source bin.
 * The resulting block always exposes a single batched `src` ghost pad and
 * updates the tails map with `"src" -> sources_bin`.
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
