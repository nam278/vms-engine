#pragma once

#include "engine/core/builders/ielement_builder.hpp"
#include "engine/core/config/config_types.hpp"

#include <gst/gst.h>

namespace engine::pipeline::builders {

/**
 * @brief Builds a single manual source bin from source config.
 *
 * The resulting per-camera bin contains `nvurisrcbin` and an optional
 * pre-mux branch such as `nvvideoconvert -> capsfilter`, then exposes a single
 * source pad for downstream batching.
 */
class NvUriSrcBinBuilder : public engine::core::builders::IElementBuilder {
   public:
    explicit NvUriSrcBinBuilder(GstElement* bin);

    GstElement* build(const engine::core::config::PipelineConfig& config, int index = 0) override;

    GstElement* build(const engine::core::config::SourcesConfig& sources,
                      const engine::core::config::CameraConfig& camera, uint32_t source_id);

   private:
    GstElement* bin_ = nullptr;
};

}  // namespace engine::pipeline::builders