/**
 * @file yaml_parser_control_api.cpp
 * @brief Parses the optional `control_api:` section into ControlApiConfig.
 */
#include "engine/infrastructure/config_parser/yaml_config_parser.hpp"
#include "yaml_parser_helpers.hpp"

namespace engine::infrastructure::config_parser {

void YamlConfigParser::parse_control_api(const void* node_ptr,
                                         engine::core::config::ControlApiConfig& out) {
    const auto& node = *static_cast<const YAML::Node*>(node_ptr);
    if (!node || !node.IsMap()) {
        return;
    }

    using helpers::yaml_bool;
    using helpers::yaml_int;
    using helpers::yaml_str;

    out.enable = yaml_bool(node, "enable", false);
    out.bind_address = yaml_str(node, "bind_address", "0.0.0.0");
    out.port = yaml_int(node, "port", 18080);
}

}  // namespace engine::infrastructure::config_parser