#pragma once
#include "engine/core/builders/ielement_builder.hpp"
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>

namespace engine::pipeline::builders {

/**
 * @brief Builds nvtracker GStreamer element from pipeline config.
 *
 * Reads config.processing.elements[index] for tracker properties
 * (ll_lib_file, ll_config_file, tracker_width/height, compute_hw).
 */
class TrackerBuilder : public engine::core::builders::IElementBuilder {
   public:
    explicit TrackerBuilder(GstElement* bin);

    GstElement* build(const engine::core::config::PipelineConfig& config, int index = 0) override;

   private:
    GstElement* bin_ = nullptr;
};

}  // namespace engine::pipeline::builders
