/**
 * @file yaml_parser_control_messaging.cpp
 * @brief Parses the top-level `control_messaging:` section.
 */
#include "engine/infrastructure/config_parser/yaml_config_parser.hpp"
#include "yaml_parser_helpers.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::infrastructure::config_parser {

void YamlConfigParser::parse_control_messaging(const void* node_ptr,
                                               engine::core::config::ControlMessagingConfig& out) {
    const auto& node = *static_cast<const YAML::Node*>(node_ptr);
    if (!node || !node.IsMap()) {
        return;
    }

    using helpers::yaml_str;

    out.enable = node["enable"] ? node["enable"].as<bool>() : false;
    out.channel = yaml_str(node, "channel", "runtime_control");
    out.reply_channel = yaml_str(node, "reply_channel", "");

    LOG_D("Parsed control_messaging section: enable={} channel='{}' reply_channel='{}'", out.enable,
          out.channel, out.reply_channel);
}

}  // namespace engine::infrastructure::config_parser