/**
 * @file yaml_config_parser.cpp
 * @brief Entry point for YamlConfigParser::parse() — dispatches to section parsers.
 */
#include "engine/infrastructure/config_parser/yaml_config_parser.hpp"
#include "engine/core/utils/logger.hpp"

#include <yaml-cpp/yaml.h>

namespace engine::infrastructure::config_parser {

bool YamlConfigParser::parse(const std::string& file_path,
                             engine::core::config::PipelineConfig& config) {
    try {
        YAML::Node root = YAML::LoadFile(file_path);
        if (!root || !root.IsMap()) {
            LOG_E("Config file '{}' is empty or not a YAML map", file_path);
            return false;
        }

        // ── version ──
        config.version = root["version"].as<std::string>("1.0.0");

        // Store sub-nodes in named locals to avoid taking address of rvalue.
        YAML::Node pipeline_node = root["pipeline"];
        YAML::Node queue_defaults_node = root["queue_defaults"];
        YAML::Node sources_node = root["sources"];
        YAML::Node processing_node = root["processing"];
        YAML::Node visuals_node = root["visuals"];
        YAML::Node outputs_node = root["outputs"];
        YAML::Node handlers_node = root["event_handlers"];
        YAML::Node messaging_node = root["messaging"];
        YAML::Node evidence_node = root["evidence"];

        // ── pipeline meta ──
        if (pipeline_node) {
            parse_pipeline(static_cast<const void*>(&pipeline_node), config.pipeline);
        }

        // ── queue defaults (must be parsed before sections that use queues) ──
        if (queue_defaults_node) {
            parse_queue_defaults(static_cast<const void*>(&queue_defaults_node),
                                 config.queue_defaults);
        }

        // ── sources ──
        if (sources_node) {
            parse_sources(static_cast<const void*>(&sources_node), config.sources,
                          config.queue_defaults);
        }

        // ── processing ──
        if (processing_node) {
            parse_processing(static_cast<const void*>(&processing_node), config.processing,
                             config.queue_defaults);
        }

        // ── visuals ──
        if (visuals_node) {
            parse_visuals(static_cast<const void*>(&visuals_node), config.visuals,
                          config.queue_defaults);
        }

        // ── outputs ──
        if (outputs_node) {
            parse_outputs(static_cast<const void*>(&outputs_node), config.outputs,
                          config.queue_defaults);
        }

        // ── event handlers ──
        if (handlers_node) {
            parse_handlers(static_cast<const void*>(&handlers_node), config.event_handlers);
        }

        // ── messaging (optional) ──
        if (messaging_node) {
            config.messaging = engine::core::config::MessagingConfig{};
            parse_messaging(static_cast<const void*>(&messaging_node), *config.messaging);
        }

        // ── evidence (optional) ──
        if (evidence_node) {
            config.evidence = engine::core::config::EvidenceConfig{};
            parse_evidence(static_cast<const void*>(&evidence_node), *config.evidence);
        }

        LOG_I("Config parsed successfully: version={} pipeline.id={}", config.version,
              config.pipeline.id);
        return true;

    } catch (const YAML::Exception& ex) {
        LOG_E("YAML parse error in '{}': {}", file_path, ex.what());
        return false;
    } catch (const std::exception& ex) {
        LOG_E("Unexpected error parsing '{}': {}", file_path, ex.what());
        return false;
    }
}

}  // namespace engine::infrastructure::config_parser
