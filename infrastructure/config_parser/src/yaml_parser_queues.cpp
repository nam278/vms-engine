/**
 * @file yaml_parser_queues.cpp
 * @brief Parses `queue_defaults:` and resolves inline `queue: {}` entries.
 */
#include "engine/infrastructure/config_parser/yaml_config_parser.hpp"
#include "yaml_parser_helpers.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::infrastructure::config_parser {

void YamlConfigParser::parse_queue_defaults(const void* node_ptr,
                                            engine::core::config::QueueConfig& out) {
    const auto& node = *static_cast<const YAML::Node*>(node_ptr);
    if (!node || !node.IsMap())
        return;

    using helpers::yaml_bool;
    using helpers::yaml_double;
    using helpers::yaml_int;

    out.max_size_buffers = yaml_int(node, "max_size_buffers", out.max_size_buffers);
    out.max_size_bytes_mb = yaml_int(node, "max_size_bytes_mb", out.max_size_bytes_mb);
    out.max_size_time_sec = yaml_double(node, "max_size_time_sec", out.max_size_time_sec);
    out.leaky = yaml_int(node, "leaky", out.leaky);
    out.silent = yaml_bool(node, "silent", out.silent);

    LOG_D("Parsed queue_defaults: buffers={} leaky={}", out.max_size_buffers, out.leaky);
}

engine::core::config::QueueConfig YamlConfigParser::resolve_queue(
    const void* node_ptr, const engine::core::config::QueueConfig& defaults) {
    const auto& node = *static_cast<const YAML::Node*>(node_ptr);

    // Start from defaults and override with explicit values
    engine::core::config::QueueConfig q = defaults;

    if (!node || !node.IsMap()) {
        // queue: {} means empty map → just use defaults
        return q;
    }

    using helpers::yaml_bool;
    using helpers::yaml_double;
    using helpers::yaml_int;

    q.max_size_buffers = yaml_int(node, "max_size_buffers", q.max_size_buffers);
    q.max_size_bytes_mb = yaml_int(node, "max_size_bytes_mb", q.max_size_bytes_mb);
    q.max_size_time_sec = yaml_double(node, "max_size_time_sec", q.max_size_time_sec);
    q.leaky = yaml_int(node, "leaky", q.leaky);
    q.silent = yaml_bool(node, "silent", q.silent);

    return q;
}

}  // namespace engine::infrastructure::config_parser
