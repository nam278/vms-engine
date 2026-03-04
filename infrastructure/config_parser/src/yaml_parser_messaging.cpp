/**
 * @file yaml_parser_messaging.cpp
 * @brief Parses the top-level `messaging:` section into MessagingConfig.
 */
#include "engine/infrastructure/config_parser/yaml_config_parser.hpp"
#include "yaml_parser_helpers.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::infrastructure::config_parser {

void YamlConfigParser::parse_messaging(const void* node_ptr,
                                       engine::core::config::MessagingConfig& out) {
    const auto& node = *static_cast<const YAML::Node*>(node_ptr);
    if (!node || !node.IsMap())
        return;

    using helpers::yaml_str;

    // "redis" is the default — allows YAML to omit `type:` for the common case
    out.type = yaml_str(node, "type", "redis");
    out.host = yaml_str(node, "host");
    out.port = node["port"] ? node["port"].as<int>() : 6379;

    LOG_D("Parsed messaging section: type='{}' host='{}' port={}", out.type, out.host, out.port);
}

}  // namespace engine::infrastructure::config_parser
