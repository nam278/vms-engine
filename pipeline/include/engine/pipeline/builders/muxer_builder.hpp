#pragma once
#include "engine/core/builders/ielement_builder.hpp"
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>

namespace engine::pipeline::builders {

/**
 * @brief Builds nvstreammux element from pipeline config.
 *
 * Reads config.sources for muxer dimensions and batch settings.
 * Normally nvmultiurisrcbin creates its own internal muxer, but this builder
 * is available if the pipeline requires a standalone nvstreammux.
 */
class MuxerBuilder : public engine::core::builders::IElementBuilder {
   public:
    explicit MuxerBuilder(GstElement* bin);

    /**
     * @brief Create and configure nvstreammux.
     * @param config Full pipeline config (reads config.sources for dimensions).
     * @param index  Ignored — single muxer per pipeline.
     * @return Configured GstElement* (bin owns), or nullptr on failure.
     */
    GstElement* build(const engine::core::config::PipelineConfig& config, int index = 0) override;

   private:
    GstElement* bin_ = nullptr;
};

}  // namespace engine::pipeline::builders
