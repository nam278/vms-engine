#pragma once
#include "engine/core/builders/ielement_builder.hpp"
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>

namespace engine::pipeline::builders {

/**
 * @brief Builds nvinfer GStreamer element (PGIE or SGIE) from pipeline config.
 *
 * Reads config.processing.elements[index].
 * Supports role: primary_inference (process_mode=1) and secondary_inference (process_mode=2).
 */
class InferBuilder : public engine::core::builders::IElementBuilder {
   public:
    explicit InferBuilder(GstElement* bin);

    /**
     * @brief Create and configure nvinfer element.
     * @param config Full pipeline config.
     * @param index  Index into config.processing.elements[].
     * @return Configured GstElement* (bin owns), or nullptr on failure.
     */
    GstElement* build(const engine::core::config::PipelineConfig& config, int index = 0) override;

   private:
    GstElement* bin_ = nullptr;
};

}  // namespace engine::pipeline::builders
