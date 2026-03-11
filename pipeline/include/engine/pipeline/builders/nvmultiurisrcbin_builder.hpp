#pragma once

#include "engine/core/builders/ielement_builder.hpp"
#include "engine/core/config/config_types.hpp"

#include <gst/gst.h>

namespace engine::pipeline::builders {

/**
 * @brief Builds a legacy nvmultiurisrcbin source block from pipeline config.
 *
 * This builder is only used when `config.sources.type == "nvmultiurisrcbin"`.
 * DeepStream documents nvmultiurisrcbin as wrapping its own internal
 * nvurisrcbin + legacy nvstreammux path, including the embedded REST server.
 */
class NvMultiUriSrcBinBuilder : public engine::core::builders::IElementBuilder {
   public:
    explicit NvMultiUriSrcBinBuilder(GstElement* bin);

    GstElement* build(const engine::core::config::PipelineConfig& config, int index = 0) override;

   private:
    GstElement* bin_ = nullptr;
};

}  // namespace engine::pipeline::builders