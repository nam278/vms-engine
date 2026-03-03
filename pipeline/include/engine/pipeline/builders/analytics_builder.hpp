#pragma once
#include "engine/core/builders/ielement_builder.hpp"
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>

namespace engine::pipeline::builders {

/**
 * @brief Builds nvdsanalytics GStreamer element from pipeline config.
 *
 * Reads config.processing.elements[index] for analytics properties
 * (config_file, gpu_id). The analytics config file defines ROI/line-crossing rules.
 */
class AnalyticsBuilder : public engine::core::builders::IElementBuilder {
   public:
    explicit AnalyticsBuilder(GstElement* bin);

    GstElement* build(const engine::core::config::PipelineConfig& config, int index = 0) override;

   private:
    GstElement* bin_ = nullptr;
};

}  // namespace engine::pipeline::builders
