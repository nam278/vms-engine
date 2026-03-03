#pragma once
#include "engine/core/builders/ielement_builder.hpp"
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>

namespace engine::pipeline::builders {

/**
 * @brief Builds nvv4l2h264enc or nvv4l2h265enc hardware encoder from config.
 *
 * Index encoding: output_index * 100 + element_index.
 * Reads config.outputs[output_index].elements[element_index].
 */
class EncoderBuilder : public engine::core::builders::IElementBuilder {
   public:
    explicit EncoderBuilder(GstElement* bin);

    GstElement* build(const engine::core::config::PipelineConfig& config, int index = 0) override;

   private:
    GstElement* bin_ = nullptr;
};

}  // namespace engine::pipeline::builders
