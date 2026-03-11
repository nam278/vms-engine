/**
 * @file yaml_parser_sources.cpp
 * @brief Parses the `sources:` section into SourcesConfig.
 */
#include "engine/infrastructure/config_parser/yaml_config_parser.hpp"
#include "yaml_parser_helpers.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::infrastructure::config_parser {

namespace {

void parse_source_branch(const YAML::Node& branch_node,
                         engine::core::config::SourceBranchConfig& out,
                         const engine::core::config::QueueConfig& defaults) {
    if (!branch_node || !branch_node.IsMap()) {
        return;
    }

    using helpers::yaml_bool;
    using helpers::yaml_int;
    using helpers::yaml_str;

    out.elements.clear();
    const YAML::Node elements = branch_node["elements"];
    if (!elements || !elements.IsSequence()) {
        return;
    }

    for (const auto& elem_node : elements) {
        engine::core::config::SourceBranchElementConfig elem;
        elem.id = yaml_str(elem_node, "id");
        elem.type = yaml_str(elem_node, "type");
        elem.enabled = yaml_bool(elem_node, "enabled", true);

        const YAML::Node props =
            elem_node["props"] && elem_node["props"].IsMap() ? elem_node["props"] : elem_node;
        elem.gpu_id = yaml_int(props, "gpu_id", 0);
        elem.caps = yaml_str(elem_node, "caps", yaml_str(props, "caps"));
        elem.nvbuf_memory_type =
            yaml_str(props, "nvbuf_memory_type", yaml_str(props, "nvbuf-memory-type"));
        elem.src_crop = yaml_str(props, "src_crop", yaml_str(props, "src-crop"));
        elem.dest_crop = yaml_str(props, "dest_crop", yaml_str(props, "dest-crop"));
        elem.queue.max_size_buffers =
            yaml_int(props, "max_size_buffers", defaults.max_size_buffers);
        elem.queue.max_size_bytes_mb =
            yaml_int(props, "max_size_bytes_mb", defaults.max_size_bytes_mb);
        if (props["max_size_time_sec"]) {
            elem.queue.max_size_time_sec = props["max_size_time_sec"].as<double>();
        } else {
            elem.queue.max_size_time_sec = defaults.max_size_time_sec;
        }
        elem.queue.leaky = yaml_int(props, "leaky", defaults.leaky);
        elem.queue.silent = yaml_bool(props, "silent", defaults.silent);
        out.elements.push_back(std::move(elem));
    }
}

void parse_source_mux(const YAML::Node& mux_node, engine::core::config::SourceMuxConfig& out) {
    if (!mux_node || !mux_node.IsMap()) {
        return;
    }

    using helpers::yaml_bool;
    using helpers::yaml_int;
    using helpers::yaml_str;

    out.id = yaml_str(mux_node, "id", out.id);
    out.implementation = yaml_str(mux_node, "implementation", out.implementation);
    out.batch_size = yaml_int(mux_node, "batch_size", out.batch_size);
    out.max_sources = yaml_int(mux_node, "max_sources", out.max_sources);
    out.batched_push_timeout_us =
        yaml_int(mux_node, "batched_push_timeout_us",
                 yaml_int(mux_node, "batched_push_timeout", out.batched_push_timeout_us));
    out.sync_inputs = yaml_bool(mux_node, "sync_inputs", out.sync_inputs);
    out.max_latency_ns = static_cast<std::uint64_t>(
        yaml_int(mux_node, "max_latency_ns", static_cast<int>(out.max_latency_ns)));
    out.drop_pipeline_eos = yaml_bool(mux_node, "drop_pipeline_eos", out.drop_pipeline_eos);
    if (mux_node["attach_sys_ts"]) {
        out.attach_sys_ts = yaml_bool(mux_node, "attach_sys_ts", false);
    }
    if (mux_node["frame_duration"]) {
        out.frame_duration = static_cast<std::int64_t>(yaml_int(mux_node, "frame_duration", 0));
    }
    out.config_file_path = yaml_str(mux_node, "config_file_path", out.config_file_path);
}

}  // namespace

void YamlConfigParser::parse_sources(const void* node_ptr, engine::core::config::SourcesConfig& out,
                                     const engine::core::config::QueueConfig& defaults) {
    const auto& node = *static_cast<const YAML::Node*>(node_ptr);
    if (!node || !node.IsMap())
        return;

    using helpers::yaml_bool;
    using helpers::yaml_int;
    using helpers::yaml_str;

    // ── Section 1: nvmultiurisrcbin direct ──
    out.id = yaml_str(node, "id", "sources");
    out.type = yaml_str(node, "type", "nvmultiurisrcbin");
    out.rest_api_port = yaml_int(node, "rest_api_port", 0);  // 0 = disable REST API
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
    if (node["drop_on_latency"]) {
        out.drop_on_latency = yaml_bool(node, "drop_on_latency", false);
    }
    out.drop_pipeline_eos = yaml_bool(node, "drop_pipeline_eos", true);
    out.async_handling = yaml_bool(node, "async_handling", true);
    out.low_latency_mode = yaml_bool(node, "low_latency_mode", false);

    // ── Section 3: nvstreammux passthrough ──
    out.width = yaml_int(node, "width", 1920);
    out.height = yaml_int(node, "height", 1080);
    out.batched_push_timeout = yaml_int(node, "batched_push_timeout", 40000);
    out.live_source = yaml_bool(node, "live_source", true);
    out.sync_inputs = yaml_bool(node, "sync_inputs", false);

    parse_source_branch(node["branch"], out.branch, defaults);
    parse_source_mux(node["mux"], out.mux);

    if (out.mux.id.empty()) {
        out.mux.id = "batch_mux";
    }
    if (out.mux.batch_size <= 0) {
        out.mux.batch_size = out.max_batch_size;
    }
    if (out.mux.max_sources <= 0) {
        out.mux.max_sources = out.mux.batch_size;
    }
    if (out.mux.batched_push_timeout_us <= 0) {
        out.mux.batched_push_timeout_us = out.batched_push_timeout;
    }

    if (out.type == "nvurisrcbin") {
        out.max_batch_size = out.mux.batch_size;
        out.batched_push_timeout = out.mux.batched_push_timeout_us;
        out.sync_inputs = out.mux.sync_inputs;
        out.drop_pipeline_eos = out.mux.drop_pipeline_eos;
    }

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
