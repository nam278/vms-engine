#pragma once
#include "engine/core/builders/ielement_builder.hpp"
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>

namespace engine::pipeline::builders {

/**
 * @brief Builds nvdsosd (On-Screen Display) GStreamer element from pipeline config.
 *
 * Reads config.visuals.elements[index] for OSD properties
 * (process_mode, display_bbox, display_text, display_mask, border_width, gpu_id).
 */
class OsdBuilder : public engine::core::builders::IElementBuilder {
   public:
    explicit OsdBuilder(GstElement* bin);

    GstElement* build(const engine::core::config::PipelineConfig& config, int index = 0) override;

   private:
    GstElement* bin_ = nullptr;
};

}  // namespace engine::pipeline::builders
