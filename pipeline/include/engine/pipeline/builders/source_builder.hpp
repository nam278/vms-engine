#pragma once
#include "engine/core/builders/ielement_builder.hpp"
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>

namespace engine::pipeline::builders {

/**
 * @brief Builds nvmultiurisrcbin element from pipeline config.
 *
 * Reads config.sources for all properties including smart record settings.
 * Camera URIs are added at runtime via nvmultiurisrcbin REST API or
 * manual sensor-id-list property.
 */
class SourceBuilder : public engine::core::builders::IElementBuilder {
   public:
    explicit SourceBuilder(GstElement* bin);

    /**
     * @brief Create and configure nvmultiurisrcbin.
     * @param config Full pipeline config (reads config.sources).
     * @param index  Ignored — single source block.
     * @return Configured GstElement* (bin owns), or nullptr on failure.
     */
    GstElement* build(const engine::core::config::PipelineConfig& config, int index = 0) override;

   private:
    GstElement* bin_ = nullptr;
};

}  // namespace engine::pipeline::builders
