#pragma once

#include "engine/core/builders/ielement_builder.hpp"
#include "engine/core/config/config_types.hpp"

#include <gst/gst.h>

namespace engine::pipeline::builders {

/**
 * @brief Builds nvvideoconvert output elements from config.
 *
 * Index encoding: output_index * 100 + element_index.
 * Reads config.outputs[output_index].elements[element_index].
 */
class VideoConvertBuilder : public engine::core::builders::IElementBuilder {
   public:
    explicit VideoConvertBuilder(GstElement* bin);

    GstElement* build(const engine::core::config::PipelineConfig& config, int index = 0) override;
    GstElement* build(const std::string& name, int gpu_id, const std::string& nvbuf_memory_type,
                      const std::string& src_crop, const std::string& dest_crop);

   private:
    GstElement* bin_ = nullptr;
};

}  // namespace engine::pipeline::builders