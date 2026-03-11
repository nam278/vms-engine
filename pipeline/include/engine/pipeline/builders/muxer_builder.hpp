#pragma once
#include "engine/core/builders/ielement_builder.hpp"
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>

namespace engine::pipeline::builders {

/**
 * @brief Builds the standalone nvstreammux used by manual nvurisrcbin mode.
 *
 * When the process enables `USE_NEW_NVSTREAMMUX=yes` before `gst_init()`, this
 * builder resolves to DeepStream's newer nvstreammux implementation. The
 * nvmultiurisrcbin path keeps using DeepStream's internal legacy mux path.
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
