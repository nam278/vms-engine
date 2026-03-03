#pragma once
#include <string>
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
    std::string type = "nvmultiurisrcbin";

    // Group 1 — nvmultiurisrcbin direct
    // NOTE: ip_address and port are not applied — DS8 ip-address property causes SIGSEGV.
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

    // Common GStreamer properties (flat — parsed from YAML key-value)
    // Each element type has different properties; store as key-value pairs
    // for flexibility. Builders extract what they need.
    std::string caps;               ///< for capsfilter
    std::string nvbuf_memory_type;  ///< for nvvideoconvert
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

struct BrokerConfig {
    std::string host;
    int port = 6379;
    std::string channel;
};

struct CleanupConfig {
    int stale_object_timeout_min = 5;
    int check_interval_batches = 30;
    int old_dirs_max_days = 7;
};

struct EventHandlerConfig {
    std::string id;
    bool enable = true;
    std::string type;            ///< "on_detect" | "on_eos" | ...
    std::string probe_element;   ///< element id to attach probe
    std::string source_element;  ///< for smart_record: "sources"
    std::string trigger;         ///< "smart_record" | "crop_object" | ...
    std::vector<std::string> label_filter;

    // Smart record specific
    int pre_event_sec = 2;
    int post_event_sec = 20;
    int min_interval_sec = 2;

    // Crop objects specific
    std::string save_dir;
    int capture_interval_sec = 5;
    int image_quality = 85;
    bool save_full_frame = true;
    std::optional<CleanupConfig> cleanup;

    // Broker (shared)
    std::optional<BrokerConfig> broker;
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
};

}  // namespace engine::core::config
