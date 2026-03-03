#pragma once
#include "engine/core/builders/ielement_builder.hpp"
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>

namespace engine::pipeline::builders {

/**
 * @brief Builds nvstreamdemux element for multi-output stream demultiplexing.
 *
 * Creates a bare nvstreamdemux — pad connections are handled dynamically
 * by the OutputsBlockBuilder via request pads.
 */
class DemuxerBuilder : public engine::core::builders::IElementBuilder {
   public:
    explicit DemuxerBuilder(GstElement* bin);

    GstElement* build(const engine::core::config::PipelineConfig& config, int index = 0) override;

   private:
    GstElement* bin_ = nullptr;
};

}  // namespace engine::pipeline::builders
