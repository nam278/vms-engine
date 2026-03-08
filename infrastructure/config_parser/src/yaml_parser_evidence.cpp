/**
 * @file yaml_parser_evidence.cpp
 * @brief Parses the top-level `evidence:` section into EvidenceConfig.
 */
#include "engine/infrastructure/config_parser/yaml_config_parser.hpp"
#include "yaml_parser_helpers.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::infrastructure::config_parser {

void YamlConfigParser::parse_evidence(const void* node_ptr,
                                      engine::core::config::EvidenceConfig& out) {
    const auto& node = *static_cast<const YAML::Node*>(node_ptr);
    if (!node || !node.IsMap())
        return;

    using helpers::yaml_bool;
    using helpers::yaml_int;
    using helpers::yaml_str;

    out.enable = yaml_bool(node, "enable", false);
    out.request_channel = yaml_str(node, "request_channel");
    out.ready_channel = yaml_str(node, "ready_channel");
    out.save_dir = yaml_str(node, "save_dir", "/opt/vms_engine/dev/rec/frames");
    out.frame_cache_ttl_ms = yaml_int(node, "frame_cache_ttl_ms", 10000);
    out.max_frame_gap_ms = yaml_int(node, "max_frame_gap_ms", 250);
    out.overview_jpeg_quality = yaml_int(node, "overview_jpeg_quality", 80);
    out.cache_on_frame_events = yaml_bool(node, "cache_on_frame_events", true);
    out.cache_backend = yaml_str(node, "cache_backend", "nvbufsurface_copy");
    out.max_frames_per_source = yaml_int(node, "max_frames_per_source", 16);

    LOG_D(
        "Parsed evidence section: enable={} request='{}' ready='{}' save_dir='{}' ttl_ms={} "
        "gap_ms={} quality={} cache_on_frame_events={} cache_backend='{}' "
        "max_frames_per_source={}",
        out.enable, out.request_channel, out.ready_channel, out.save_dir, out.frame_cache_ttl_ms,
        out.max_frame_gap_ms, out.overview_jpeg_quality, out.cache_on_frame_events,
        out.cache_backend, out.max_frames_per_source);
}

}  // namespace engine::infrastructure::config_parser