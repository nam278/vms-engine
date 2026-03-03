#pragma once
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>

namespace engine::core::builders {

/**
 * @brief Builds a single GStreamer element from the full pipeline config.
 * @param config Full PipelineConfig — builder accesses its relevant section.
 * @param index  Index into a repeated section (e.g., processing.elements[index]).
 * @return Raw GstElement* — ownership transfers to GstBin after gst_bin_add().
 */
class IElementBuilder {
   public:
    virtual ~IElementBuilder() = default;
    virtual GstElement* build(const engine::core::config::PipelineConfig& config,
                              int index = 0) = 0;
};

}  // namespace engine::core::builders
