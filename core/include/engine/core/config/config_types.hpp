#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>

namespace engine::core::config {

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Queue Config (shared — queue_defaults + inline queue: {} overrides)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

struct QueueConfig {
    int max_size_buffers = 10;
    int max_size_bytes_mb = 20;
    double max_size_time_sec = 0.5;
    int leaky = 2;  ///< 0=none, 1=upstream, 2=downstream
    bool silent = true;
};

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Pipeline Metadata
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

struct PipelineMetaConfig {
    std::string id;
    std::string name;
    std::string log_level = "INFO";  ///< DEBUG | INFO | WARN | ERROR
    std::string gst_log_level = "*:1";
    std::string dot_file_dir;  ///< empty = disabled
    std::string log_file;      ///< empty = stdout only
};

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Sources Block (nvmultiurisrcbin)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

struct CameraConfig {
    std::string id;  ///< camera identifier, used as element name
    std::string uri;
};

struct SourcesConfig {
    std::string id = "sources";  ///< stable element id/name for the source bin
    std::string type = "nvmultiurisrcbin";

    // Group 1 — nvmultiurisrcbin direct
    // NOTE: ip_address is never set — DS8 ip-address property causes SIGSEGV.
    // rest_api_port controls the built-in CivetWeb REST API port (0 = disable).
    int rest_api_port = 0;  ///< 0=disable REST API, >0 = bind on that port (default DS9000)
    int max_batch_size = 4;
    int mode = 0;  ///< 0=video, 1=audio

    // Group 2 — nvurisrcbin per-source passthrough
    int gpu_id = 0;
    int num_extra_surfaces = 9;
    int cudadec_memtype = 0;  ///< 0=device, 1=pinned, 2=unified
    int dec_skip_frames = 0;  ///< 0=all, 1=non-ref, 2=key-only
    int drop_frame_interval = 0;
    int select_rtp_protocol = 4;  ///< 0=multi, 4=TCP-only
    int rtsp_reconnect_interval = 10;
    int rtsp_reconnect_attempts = -1;
    int init_rtsp_reconnect_interval = -1;  ///< -1=fallback to rtsp_reconnect_interval, 0=disable
    int latency = 400;
    int udp_buffer_size = 4194304;
    bool file_loop = false;  ///< loop file:// sources after EOS
    bool disable_audio = false;
    bool disable_passthrough = false;
    bool drop_pipeline_eos = true;
    bool async_handling = true;     ///< handle async state changes (default: true)
    bool low_latency_mode = false;  ///< low-latency mode for I/IPPP bitstreams

    // Group 3 — nvstreammux passthrough
    int width = 1920;
    int height = 1080;
    int batched_push_timeout = 40000;  ///< µs
    bool live_source = true;
    bool sync_inputs = false;

    // Cameras
    std::vector<CameraConfig> cameras;

    // Smart Record — flat properties on nvmultiurisrcbin
    int smart_record = 0;  ///< 0=disable, 1=cloud-only, 2=multi
    std::string smart_rec_dir_path;
    std::string smart_rec_file_prefix = "lsr";
    int smart_rec_cache = 10;  ///< pre-event audio+video buffer (sec)
    int smart_rec_default_duration = 20;
    int smart_rec_mode = 0;       ///< 0=audio+video, 1=video, 2=audio
    int smart_rec_container = 0;  ///< 0=mp4, 1=mkv
};

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Processing Block (nvinfer, nvtracker, nvdsanalytics, ...)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

struct ProcessingElementConfig {
    std::string id;
    std::string type;  ///< "nvinfer" | "nvtracker" | "nvdsanalytics" | ...
    std::string role;  ///< "primary_inference" | "tracker" | "analytics" | ...

    // nvinfer properties
    int unique_id = 0;
    std::string config_file;
    int process_mode = 1;  ///< 1=primary, 2=secondary
    int interval = 0;
    int batch_size = 4;
    int gpu_id = 0;
    int operate_on_gie_id = -1;
    std::string operate_on_class_ids;  ///< "0:2" format

    // nvtracker properties
    std::string ll_lib_file;
    std::string ll_config_file;
    int tracker_width = 640;
    int tracker_height = 640;
    int compute_hw = 0;  ///< 0=default, 1=GPU, 2=VIC
    bool display_tracking_id = true;
    int user_meta_pool_size = 512;

    // nvdsanalytics properties
    // (config_file shared with nvinfer — same field)

    // Inline queue
    bool has_queue = false;  ///< true if queue: {} present in YAML
    QueueConfig queue;
};

struct ProcessingConfig {
    std::vector<ProcessingElementConfig> elements;
};

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Visuals Block (nvmultistreamtiler, nvdsosd)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

struct VisualsElementConfig {
    std::string id;
    std::string type;  ///< "nvmultistreamtiler" | "nvdsosd"
    int gpu_id = 0;

    // nvmultistreamtiler
    int rows = 1;
    int columns = 1;
    int width = 1920;
    int height = 1080;

    // nvdsosd
    int process_mode = 1;  ///< 0=cpu, 1=gpu, 2=auto
    bool display_bbox = true;
    bool display_text = false;
    bool display_mask = false;
    int border_width = 2;

    // Inline queue
    bool has_queue = false;
    QueueConfig queue;
};

struct VisualsConfig {
    bool enable = true;
    std::vector<VisualsElementConfig> elements;
};

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Outputs Block (arrays of flat element chains)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

struct OutputElementConfig {
    std::string id;
    std::string type;  ///< GStreamer factory name
    int gpu_id = 0;

    // Common GStreamer properties (flat — parsed from YAML key-value)
    // Each element type has different properties; store as key-value pairs
    // for flexibility. Builders extract what they need.
    std::string caps;               ///< for capsfilter
    std::string nvbuf_memory_type;  ///< for nvvideoconvert
    std::string src_crop;           ///< for nvvideoconvert
    std::string dest_crop;          ///< for nvvideoconvert
    int bitrate = 0;                ///< for encoder (bps)
    std::string control_rate;       ///< for encoder
    std::string profile;            ///< for encoder
    int iframeinterval = 0;         ///< for encoder
    std::string location;           ///< for sink (RTSP URL, file path)
    std::string protocols;          ///< for rtspclientsink

    // Inline queue
    bool has_queue = false;
    QueueConfig queue;
};

struct OutputConfig {
    std::string id;
    std::string type;  ///< "rtsp_client" | "file" | "display" | "fake"
    std::vector<OutputElementConfig> elements;
};

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Event Handlers (pad probe callbacks)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

/**
 * @brief Top-level messaging/producer configuration.
 *
 * Controls which message broker the engine publishes events to.
 * Set `type` to "redis" (default) or "kafka".
 * If omitted from YAML, producer is disabled (publish calls are no-ops).
 */
struct MessagingConfig {
    std::string type = "redis";  ///< "redis" | "kafka"
    std::string host;            ///< broker hostname / IP
    int port = 6379;             ///< broker port (Redis default 6379)
};

struct CleanupConfig {
    ///< Drop stale per-object state after N unseen minutes.
    int stale_object_timeout_min = 5;
    ///< Run cleanup every N processed batches instead of every frame.
    int check_interval_batches = 30;
    ///< Delete output directories older than N days; 0 disables directory cleanup.
    int old_dirs_max_days = 7;
};

/** @brief A single external processing rule (e.g., face recognition). */
struct ExtProcessorRule {
    std::string label;         ///< Object label to match (e.g. "face")
    std::string endpoint;      ///< HTTP endpoint URL
    std::string result_path;   ///< JSON path to result (e.g. "match.external_id")
    std::string display_path;  ///< JSON path to display (e.g. "match.face_name")
    std::unordered_map<std::string, std::string> params;  ///< Additional query parameters
};

/** @brief External processing service configuration (e.g., face recognition). */
struct ExtProcessorConfig {
    bool enable = false;
    int min_interval_sec = 1;
    std::vector<ExtProcessorRule> rules;
};

/** @brief One frame-events external processing rule (e.g. face-rec, LPR). */
struct FrameEventsExtProcRule {
    std::string label;         ///< Object label to match (e.g. "face")
    std::string endpoint;      ///< HTTP endpoint URL
    std::string result_path;   ///< JSON path to primary result value
    std::string display_path;  ///< JSON path to human-readable display text
    std::unordered_map<std::string, std::string> params;  ///< Extra query parameters
};

/**
 * @brief Async external enrichment sidecar for `trigger: frame_events`.
 *
 * Unlike the legacy root-level `ext_processor` block used by `crop_objects`,
 * this config is namespaced under `frame_events:` and is invoked only after a
 * semantic frame has already been published.
 */
struct FrameEventsExtProcConfig {
    bool enable = false;
    std::string publish_channel;  ///< Dedicated broker channel/topic for ext_proc events.
    int jpeg_quality = 85;
    int connect_timeout_ms = 5000;
    int request_timeout_ms = 10000;
    bool emit_empty_result = false;
    bool include_overview_ref = true;
    std::vector<FrameEventsExtProcRule> rules;
};

/**
 * @brief Semantic emit policy for `trigger: frame_events`.
 *
 * These knobs control when a source-frame is important enough to publish to the
 * downstream business-event layer. They do not affect evidence encoding.
 */
struct FrameEventsConfig {
    ///< Heartbeat cadence while the scene stays semantically stable.
    int heartbeat_interval_ms = 1000;
    ///< Minimum spacing between two emitted messages from the same source.
    int min_emit_gap_ms = 250;
    ///< Lower IoU means the object moved enough to count as `motion_change`.
    ///< Only used when `emit_on_motion_change` is enabled.
    double motion_iou_threshold = 0.85;
    ///< Center shift threshold, relative to the previous bbox diagonal.
    ///< Only used when `emit_on_motion_change` is enabled.
    double center_shift_ratio_threshold = 0.05;
    ///< Emit when an existing tracked object moves enough to cross geometry thresholds.
    bool emit_on_motion_change = false;
    ///< Emit immediately when a source transitions from no detections to detections.
    bool emit_on_first_frame = true;
    ///< Emit when the tracked object membership for the frame changes.
    bool emit_on_object_set_change = true;
    ///< Emit when class_id, object_type, or stable SGIE labels change.
    bool emit_on_label_change = true;
    ///< Sliding window size for SGIE label majority vote per tracked object.
    int label_vote_window_frames = 5;
    ///< Emit when a child object is re-parented to a different parent track.
    bool emit_on_parent_change = true;
    ///< If true, allow empty semantic frames to be published.
    bool emit_empty_frames = false;
    ///< Optional async external enrichment sidecar for emitted frame_events.
    std::optional<FrameEventsExtProcConfig> ext_processor;
};

/**
 * @brief Top-level request-driven evidence workflow configuration.
 *
 * `frame_events` stays semantic-only. This block controls the short-lived cache
 * and the completion stream used after downstream publishes `evidence_request`.
 */
struct EvidenceConfig {
    bool enable = false;
    ///< Stream/topic consumed by the engine for evidence materialization requests.
    std::string request_channel;
    ///< Stream/topic published by the engine after evidence encode completes.
    std::string ready_channel;
    ///< Base directory used to materialize overview/crop images on disk.
    std::string save_dir = "/opt/vms_engine/dev/rec/frames";
    ///< TTL for cached emitted frames.
    int frame_cache_ttl_ms = 10000;
    ///< Maximum timestamp delta accepted when exact frame_key lookup misses.
    int max_frame_gap_ms = 250;
    ///< JPEG quality currently shared by overview and crop outputs.
    int overview_jpeg_quality = 80;
    ///< If false, semantic emits do not populate the evidence cache.
    bool cache_on_frame_events = true;
    ///< Snapshot backend name; currently `nvbufsurface_copy`.
    std::string cache_backend = "nvbufsurface_copy";
    ///< Hard bound per `(pipeline_id, source_name, source_id)` cache queue.
    int max_frames_per_source = 16;
    ///< TTL for the in-memory dedupe ledger of recently materialized refs.
    int encode_dedupe_ttl_ms = 30000;
    ///< Hard bound for recently materialized refs tracked by the dedupe ledger.
    int max_recent_encoded_refs = 256;
};

struct EventHandlerConfig {
    std::string id;
    bool enable = true;
    std::string type;              ///< "on_detect" | "on_eos" | ...
    std::string probe_element;     ///< element id to attach probe
    std::string pad_name = "src";  ///< pad to probe: "src" (default) or "sink"
    std::string
        source_element;  ///< for smart_record: source element id/name (defaults to sources.id)
    std::string
        trigger;  ///< "smart_record" | "crop_objects" | "class_id_offset" | "class_id_restore"
    std::string channel;  ///< message broker channel/topic to publish to (e.g. "worker_lsr")
    std::vector<std::string> label_filter;

    // Smart record specific
    int pre_event_sec = 2;
    int post_event_sec = 20;
    int min_interval_sec = 2;
    int max_concurrent_recordings = 0;  ///< 0 = unlimited

    // Crop objects specific
    std::string save_dir;
    int capture_interval_sec = 5;
    int image_quality = 85;
    bool save_full_frame = true;
    int burst_max = 3;
    int k_on_frames = 5;
    int k_off_frames = 2;
    int k_label_frames = 5;
    double token_refill_sec = 5.0;
    double bypass_min_gap_sec = 1.0;
    std::optional<CleanupConfig> cleanup;

    // External processing (e.g., face recognition via HTTP)
    std::optional<ExtProcessorConfig> ext_processor;

    // Frame-events specific
    std::optional<FrameEventsConfig> frame_events;
};

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Root Config — the single top-level struct
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

struct PipelineConfig {
    std::string version;

    PipelineMetaConfig pipeline;
    QueueConfig queue_defaults;
    SourcesConfig sources;
    ProcessingConfig processing;
    VisualsConfig visuals;
    std::vector<OutputConfig> outputs;
    std::vector<EventHandlerConfig> event_handlers;

    /** @brief Optional top-level message broker (producer). Absent = no publishing. */
    std::optional<MessagingConfig> messaging;

    /** @brief Optional top-level evidence request/completion workflow configuration. */
    std::optional<EvidenceConfig> evidence;
};

}  // namespace engine::core::config
