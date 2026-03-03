#pragma once
#include "engine/core/builders/ielement_builder.hpp"
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>

namespace engine::pipeline::builders {

/**
 * @brief Builds nvmultistreamtiler GStreamer element from pipeline config.
 *
 * Reads config.visuals.elements[index] for rows, columns, width, height, gpu_id.
 */
class TilerBuilder : public engine::core::builders::IElementBuilder {
   public:
    explicit TilerBuilder(GstElement* bin);

    GstElement* build(const engine::core::config::PipelineConfig& config, int index = 0) override;

   private:
    GstElement* bin_ = nullptr;
};

}  // namespace engine::pipeline::builders
