#pragma once

#include "engine/core/builders/ielement_builder.hpp"
#include "engine/core/config/config_types.hpp"

#include <gst/gst.h>

namespace engine::pipeline::builders {

/**
 * @brief Builds capsfilter output elements from config.
 *
 * Index encoding: output_index * 100 + element_index.
 * Reads config.outputs[output_index].elements[element_index].
 */
class CapsFilterBuilder : public engine::core::builders::IElementBuilder {
   public:
    explicit CapsFilterBuilder(GstElement* bin);

    GstElement* build(const engine::core::config::PipelineConfig& config, int index = 0) override;
    GstElement* build(const std::string& name, const std::string& caps);

   private:
    GstElement* bin_ = nullptr;
};

}  // namespace engine::pipeline::builders