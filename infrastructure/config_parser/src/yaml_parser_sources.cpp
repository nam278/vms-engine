/**
 * @file yaml_parser_sources.cpp
 * @brief Parses the `sources:` section into SourcesConfig.
 */
#include "engine/infrastructure/config_parser/yaml_config_parser.hpp"
#include "yaml_parser_helpers.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::infrastructure::config_parser {

void YamlConfigParser::parse_sources(const void* node_ptr, engine::core::config::SourcesConfig& out,
                                     const engine::core::config::QueueConfig& defaults) {
    const auto& node = *static_cast<const YAML::Node*>(node_ptr);
    if (!node || !node.IsMap())
        return;

    using helpers::yaml_bool;
    using helpers::yaml_int;
    using helpers::yaml_str;

    // ── Section 1: nvmultiurisrcbin direct ──
    out.type = yaml_str(node, "type", "nvmultiurisrcbin");
    out.max_batch_size = yaml_int(node, "max_batch_size", 4);
    out.mode = yaml_int(node, "mode", 0);

    // ── Section 2: nvurisrcbin per-source passthrough ──
    out.gpu_id = yaml_int(node, "gpu_id", 0);
    out.num_extra_surfaces = yaml_int(node, "num_extra_surfaces", 9);
    out.cudadec_memtype = yaml_int(node, "cudadec_memtype", 0);
    out.dec_skip_frames = yaml_int(node, "dec_skip_frames", 0);
    out.drop_frame_interval = yaml_int(node, "drop_frame_interval", 0);
    out.select_rtp_protocol = yaml_int(node, "select_rtp_protocol", 4);
    out.rtsp_reconnect_interval = yaml_int(node, "rtsp_reconnect_interval", 10);
    out.rtsp_reconnect_attempts = yaml_int(node, "rtsp_reconnect_attempts", -1);
    out.init_rtsp_reconnect_interval = yaml_int(node, "init_rtsp_reconnect_interval", -1);
    out.latency = yaml_int(node, "latency", 400);
    out.udp_buffer_size = yaml_int(node, "udp_buffer_size", 4194304);
    out.file_loop = yaml_bool(node, "file_loop", false);
    out.disable_audio = yaml_bool(node, "disable_audio", false);
    out.disable_passthrough = yaml_bool(node, "disable_passthrough", false);
    out.drop_pipeline_eos = yaml_bool(node, "drop_pipeline_eos", true);
    out.async_handling = yaml_bool(node, "async_handling", true);
    out.low_latency_mode = yaml_bool(node, "low_latency_mode", false);

    // ── Section 3: nvstreammux passthrough ──
    out.width = yaml_int(node, "width", 1920);
    out.height = yaml_int(node, "height", 1080);
    out.batched_push_timeout = yaml_int(node, "batched_push_timeout", 40000);
    out.live_source = yaml_bool(node, "live_source", true);
    out.sync_inputs = yaml_bool(node, "sync_inputs", false);

    // ── Cameras ──
    out.cameras.clear();
    if (node["cameras"] && node["cameras"].IsSequence()) {
        for (const auto& cam_node : node["cameras"]) {
            engine::core::config::CameraConfig cam;
            cam.id = yaml_str(cam_node, "id");
            cam.uri = yaml_str(cam_node, "uri");
            out.cameras.push_back(std::move(cam));
        }
    }
    LOG_D("Parsed {} cameras", out.cameras.size());

    // ── Smart Record ──
    out.smart_record = yaml_int(node, "smart_record", 0);
    out.smart_rec_dir_path = yaml_str(node, "smart_rec_dir_path");
    out.smart_rec_file_prefix = yaml_str(node, "smart_rec_file_prefix", "lsr");
    out.smart_rec_cache = yaml_int(node, "smart_rec_cache", 10);
    out.smart_rec_default_duration = yaml_int(node, "smart_rec_default_duration", 20);
    out.smart_rec_mode = yaml_int(node, "smart_rec_mode", 0);
    out.smart_rec_container = yaml_int(node, "smart_rec_container", 0);
}

}  // namespace engine::infrastructure::config_parser
