#pragma once
#include "engine/core/config/config_types.hpp"
#include <string>
#include <vector>

namespace engine::pipeline {

/**
 * @brief Validates PipelineConfig before pipeline construction.
 *
 * Checks: required fields present, IDs unique, element types recognized,
 * inline queue references valid, output configs well-formed.
 */
class ConfigValidator {
   public:
    /**
     * @brief Validate a full pipeline config.
     * @param config The config to validate.
     * @return true if validation passes; false with errors logged.
     */
    bool validate(const engine::core::config::PipelineConfig& config) const;

    /**
     * @brief Get the list of validation errors from the last validate() call.
     */
    const std::vector<std::string>& errors() const {
        return errors_;
    }

   private:
    mutable std::vector<std::string> errors_;

    bool validate_pipeline_meta(const engine::core::config::PipelineMetaConfig& meta) const;
    bool validate_sources(const engine::core::config::SourcesConfig& sources) const;
    bool validate_processing(const engine::core::config::ProcessingConfig& processing) const;
    bool validate_visuals(const engine::core::config::VisualsConfig& visuals) const;
    bool validate_outputs(const std::vector<engine::core::config::OutputConfig>& outputs) const;
    bool validate_handlers(
        const std::vector<engine::core::config::EventHandlerConfig>& handlers) const;
};

}  // namespace engine::pipeline
