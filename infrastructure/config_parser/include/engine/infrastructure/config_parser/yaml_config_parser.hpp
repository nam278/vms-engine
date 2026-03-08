#pragma once

#include "engine/core/config/iconfig_parser.hpp"
#include "engine/core/config/config_types.hpp"
#include <string>

namespace engine::infrastructure::config_parser {

/**
 * @brief YAML config parser producing flat, DeepStream-native PipelineConfig.
 *
 * Implements engine::core::config::IConfigParser.
 * Each YAML section is parsed by a dedicated source file (yaml_parser_*.cpp)
 * that operates on the same YamlConfigParser instance.
 */
class YamlConfigParser : public engine::core::config::IConfigParser {
   public:
    /**
     * @brief Parse a YAML file into PipelineConfig.
     * @param file_path Path to the YAML config file.
     * @param config    Output config struct to populate.
     * @return true if parsing succeeded with no fatal errors.
     */
    bool parse(const std::string& file_path, engine::core::config::PipelineConfig& config) override;

    // ── Section parsers (called by parse(), implemented in separate .cpp files) ──

    /** @brief Parse top-level `pipeline:` section. */
    void parse_pipeline(const void* node, engine::core::config::PipelineMetaConfig& out);

    /** @brief Parse `queue_defaults:` section. */
    void parse_queue_defaults(const void* node, engine::core::config::QueueConfig& out);

    /** @brief Resolve an inline `queue:` node, merging with defaults. */
    engine::core::config::QueueConfig resolve_queue(
        const void* node, const engine::core::config::QueueConfig& defaults);

    /** @brief Parse `sources:` section. */
    void parse_sources(const void* node, engine::core::config::SourcesConfig& out,
                       const engine::core::config::QueueConfig& defaults);

    /** @brief Parse `processing:` section. */
    void parse_processing(const void* node, engine::core::config::ProcessingConfig& out,
                          const engine::core::config::QueueConfig& defaults);

    /** @brief Parse `visuals:` section. */
    void parse_visuals(const void* node, engine::core::config::VisualsConfig& out,
                       const engine::core::config::QueueConfig& defaults);

    /** @brief Parse `outputs:` section. */
    void parse_outputs(const void* node, std::vector<engine::core::config::OutputConfig>& out,
                       const engine::core::config::QueueConfig& defaults);

    /** @brief Parse `event_handlers:` section. */
    void parse_handlers(const void* node,
                        std::vector<engine::core::config::EventHandlerConfig>& out);

    /** @brief Parse `messaging:` section (optional top-level producer config). */
    void parse_messaging(const void* node, engine::core::config::MessagingConfig& out);

    /** @brief Parse `evidence:` section (optional top-level evidence workflow config). */
    void parse_evidence(const void* node, engine::core::config::EvidenceConfig& out);
};

}  // namespace engine::infrastructure::config_parser
