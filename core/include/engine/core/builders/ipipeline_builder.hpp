#pragma once
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>
#include <glib.h>

namespace engine::core::builders {

/**
 * @brief Builds the complete GStreamer pipeline from config.
 */
class IPipelineBuilder {
   public:
    virtual ~IPipelineBuilder() = default;
    virtual bool build(const engine::core::config::PipelineConfig& config,
                       GMainLoop* main_loop) = 0;
    virtual GstElement* get_pipeline() const = 0;
};

}  // namespace engine::core::builders
