/**
 * @file yaml_parser_pipeline.cpp
 * @brief Parses the top-level `pipeline:` section into PipelineMetaConfig.
 */
#include "engine/infrastructure/config_parser/yaml_config_parser.hpp"
#include "yaml_parser_helpers.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::infrastructure::config_parser {

void YamlConfigParser::parse_pipeline(const void* node_ptr,
                                      engine::core::config::PipelineMetaConfig& out) {
    const auto& node = *static_cast<const YAML::Node*>(node_ptr);
    if (!node || !node.IsMap())
        return;

    using helpers::yaml_str;

    out.id = yaml_str(node, "id");
    out.name = yaml_str(node, "name");
    out.log_level = yaml_str(node, "log_level", "INFO");
    out.gst_log_level = yaml_str(node, "gst_log_level", "*:1");
    out.dot_file_dir = yaml_str(node, "dot_file_dir");
    out.log_file = yaml_str(node, "log_file");

    LOG_D("Parsed pipeline section: id='{}' name='{}'", out.id, out.name);
}

}  // namespace engine::infrastructure::config_parser
