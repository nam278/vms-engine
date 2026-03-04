# 05. Configuration System — YAML Config Schema

## 1. Tổng quan

VMS Engine hoàn toàn **config-driven**: pipeline topology, inference models, output sinks, smart record, messaging — tất cả được định nghĩa trong file YAML. Không cần recompile để thay đổi deployment.

File ref: [`../../configs/deepstream_default.yml`](../../configs/deepstream_default.yml)

## 2. YAML Conventions Bắt Buộc

### 2.1 Property Names

- YAML dùng `snake_case` (e.g., `config_file_path`, `ll_lib_file`)
- GStreamer properties dùng `kebab-case` (e.g., `config-file-path`, `ll-lib-file`)
- YAML parser tự động convert `_` → `-` khi set GStreamer properties

```yaml
# YAML (snake_case)          GStreamer property (kebab-case)
config_file_path: …     →   config-file-path
ll_lib_file: …     →   ll-lib-file
max_size_buffers: …     →   max-size-buffers
```

### 2.2 Enum Fields là Integers

Tất cả enum properties trong GStreamer được represent bởi **integer** trong YAML:

```yaml
smart_record: 1 # 0=off, 1=audio+video, 2=video-only
process_mode: 1 # 1=primary (PGIE), 2=secondary (SGIE)
compute_hw: 0 # 0=default, 1=GPU, 2=VIC
```

### 2.3 `queue: {}` Pattern

Thêm `queue: {}` vào bất kỳ element nào để auto-insert GstQueue với default settings trước element đó:

```yaml
- id: "pgie"
  queue: {} # Insert queue trước pgie

# Hoặc override defaults:
- id: "tracker"
  queue:
    max_size_buffers: 20
    leaky: 2
```

## 3. Schema Đầy Đủ

> **Canonical reference**: [`docs/configs/deepstream_default.yml`](../../configs/deepstream_default.yml)

```yaml
# =============================================================================
# VMS Engine — DeepStream Pipeline Configuration
# =============================================================================
# PIPELINE TOPOLOGY (left-to-right, each stage = GstBin with ghost pads):
#   [sources_bin] →
#   [processing_bin] →
#   [visuals_bin] →
#   [output_bin_{id}]
#
# QUEUE RULES:
#   queue: {}         → insert queue before this element (inside its bin)
#   queue: { ... }    → insert queue, override specific fields
#   (no queue field)  → no queue before this element
# =============================================================================

version: "1.0.0"

# ─────────────────────────────────────────────────────────────────────
# Pipeline-level metadata
# ─────────────────────────────────────────────────────────────────────
pipeline:
  id: "de1"
  name: "Intrusion Detection Pipeline"
  log_level: "INFO" # DEBUG | INFO | WARN | ERROR
  gst_log_level: "*:1" # GStreamer categories, e.g. "*:3,GST_PADS:5"
  dot_file_dir: "/opt/engine/data/logs"
  log_file: "/opt/engine/data/logs/app.log"

# Queue defaults — any queue: {} with no overrides inherits these
queue_defaults:
  max_size_buffers: 10
  max_size_bytes_mb: 20
  max_size_time_sec: 0.5
  leaky: 2 # 0=none, 1=upstream, 2=downstream
  silent: true

# =============================================================================
# STAGE 1 — Sources Block (→ sources_bin)
# Element: nvmultiurisrcbin
# Properties in 3 groups: direct, nvurisrcbin passthrough, nvstreammux passthrough
# =============================================================================
sources:
  type: nvmultiurisrcbin

  # Group 1 — nvmultiurisrcbin direct
  # NOTE: ip_address and port are NOT configured — DS8 ip-address setter causes SIGSEGV.
  # REST API is disabled by default; element uses 0.0.0.0 internally.
  max_batch_size: 4
  mode: 0 # 0=video  1=audio

  # Group 2 — nvurisrcbin per-source passthrough
  gpu_id: 0
  num_extra_surfaces: 9
  cudadec_memtype: 0 # 0=device  1=pinned  2=unified
  dec_skip_frames: 0 # 0=all  1=non-ref  2=key-only
  drop_frame_interval: 0
  select_rtp_protocol: 4 # 0=multi  4=TCP-only
  rtsp_reconnect_interval: 10
  rtsp_reconnect_attempts: -1
  latency: 400
  udp_buffer_size: 4194304
  disable_audio: false
  disable_passthrough: false
  drop_pipeline_eos: true

  # Group 3 — nvstreammux passthrough
  width: 1920
  height: 1080
  batched_push_timeout: 40000 # µs
  live_source: true
  sync_inputs: false

  cameras:
    - id: camera-01
      uri: rtsp://192.168.1.99:8554/view_cam_camera-01
    - id: camera-02
      uri: rtsp://192.168.1.99:8554/view_cam_camera-02

  # Smart Record — flat properties on nvmultiurisrcbin
  smart_record: 2 # 0=disable  1=cloud-only  2=multi
  smart_rec_dir_path: "/opt/engine/data/rec"
  smart_rec_file_prefix: "lsr"
  smart_rec_cache: 10 # pre-event buffer (sec)
  smart_rec_default_duration: 20
  smart_rec_mode: 0 # 0=audio+video  1=video  2=audio
  smart_rec_container: 0 # 0=mp4  1=mkv

# =============================================================================
# STAGE 2 — Processing Block (→ processing_bin)
# Elements: nvinfer | nvinferserver, nvtracker, nvstreamdemux
# =============================================================================
processing:
  elements:
    - id: pgie_detection
      type: nvinfer # nvinfer | nvinferserver
      role: primary_inference
      unique_id: 1
      config_file: "/opt/engine/data/components/pgie_detection/config.yml"
      process_mode: 1 # 1=primary  2=secondary
      interval: 3
      batch_size: 4
      gpu_id: 0
      queue: {}

    - id: tracker
      type: nvtracker
      ll_lib_file: "/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so"
      ll_config_file: "/opt/engine/data/config/tracker_NvDCF_perf.yml"
      tracker_width: 640
      tracker_height: 640
      gpu_id: 0
      compute_hw: 1 # 0=default  1=GPU  2=VIC
      user_meta_pool_size: 512
      queue: {}

# =============================================================================
# STAGE 3 — Visuals Block (→ visuals_bin)
# =============================================================================
visuals:
  enable: true
  elements:
    - id: tiler
      type: nvmultistreamtiler
      gpu_id: 0
      rows: 2
      columns: 2
      width: 1920
      height: 1080
      queue: {}

    - id: osd
      type: nvdsosd
      gpu_id: 0
      process_mode: 1 # 0=cpu  1=gpu  2=auto
      display_bbox: true
      display_text: false
      display_mask: false
      queue: {}

# =============================================================================
# STAGE 4 — Outputs Block (→ outputs_bin)
# Each output = flat elements[] list in GStreamer link order
# =============================================================================
outputs:
  - id: rtsp_out
    type: rtsp_client
    elements:
      - id: preencode_convert
        type: nvvideoconvert
        nvbuf_memory_type: nvbuf-mem-cuda-device
        queue: {}

      - id: preencode_caps
        type: capsfilter
        caps: "video/x-raw(memory:NVMM), format=(string)NV12"

      - id: encoder
        type: nvv4l2h264enc
        bitrate: 3000000
        control_rate: cbr
        profile: main
        iframeinterval: 30

      - id: parser
        type: h264parse
        queue:
          max_size_buffers: 20
          leaky: 2

      - id: sink
        type: rtspclientsink
        location: rtsp://192.168.1.99:8554/de1
        protocols: tcp
        queue: {}

# =============================================================================
# MESSAGING — Centralized broker config (Redis / Kafka)
# =============================================================================
messaging:
  type: redis # "redis" | "kafka"
  host: 192.168.1.99
  port: 6379 # Redis default; use 9092 for Kafka
### Reconnect Behavior

**Redis** (hiredis):
- Background thread with exponential backoff (5s → 10s → ... → 60s max)
- Retries forever; messages are dropped + logged if broker is down
- Reconnect starts immediately when a publish fails

**Kafka** (librdkafka):
- Built-in broker-level reconnect with exponential backoff (5s initial → 60s max)
- Messages are queued indefinitely (`message.timeout.ms=0`); broker reconnect is automatic
- Once enqueued, messages will eventually be delivered even if broker is down for hours
- No application thread needed — librdkafka manages everything internally
# =============================================================================
# EVENT HANDLERS — GStreamer pad probe callbacks
# =============================================================================
event_handlers:
  - id: smart_record
    enable: true
    type: on_detect
    probe_element: tracker
    source_element: sources
    trigger: smart_record
    channel: worker_lsr # Redis Stream / Kafka topic to publish events to
    label_filter: [bike, bus, car, person, truck]
    pre_event_sec: 2
    post_event_sec: 20
    min_interval_sec: 2

  - id: crop_objects
    enable: true
    type: on_detect
    probe_element: tracker
    trigger: crop_object
    channel: worker_lsr_snap # Redis Stream / Kafka topic to publish crop events
    label_filter: [bike, bus, car, person, truck]
    save_dir: "/opt/engine/data/rec/objects"
    capture_interval_sec: 5
    image_quality: 85
    save_full_frame: true
    cleanup:
      stale_object_timeout_min: 5
      check_interval_batches: 30
      old_dirs_max_days: 7
```

## 4. nvinfer Config File (`.txt`)

Các nvinfer properties trong YAML chỉ set GStreamer element properties. Model details được config trong file `.txt` riêng:

```ini
# configs/nvinfer/pgie_config.txt (DeepStream nvinfer format)
[property]
gpu-id=0
net-scale-factor=0.00392156862745098
model-color-format=0
gie-unique-id=1
network-type=0                         # 0=Detector, 1=Classifier, 2=Segmentation
num-detected-classes=80
interval=0
labelfile-path=configs/nvinfer/labels.txt
engine-create-func-name=NvDsInferYoloEngine

[class-attrs-all]
pre-cluster-threshold=0.25
topk=300
nms-threshold=0.5
```

## 5. Tracker Config File (`.yml`)

```yaml
# configs/tracker/nvdcf_config.yml
BaseConfig:
  minDetectorConfidence: 0.6

TargetManagement:
  maxTargetsPerStream: 99
  enableBboxUnClipping: 1

PreProcessor:
  pixelFormat: 3 # 3=BGR, 1=GRAY

DataAssociator:
  useMatching: 1
```

## 6. YAML Parsing Architecture

```
YamlConfigParser::parse(path)
  ├── parse_pipeline_meta()       → PipelineConfig.pipeline
  ├── parse_queue_defaults()      → PipelineConfig.queue_defaults
  ├── parse_sources()             → PipelineConfig.sources
  │     ├── cameras[]
  │     └── smart_record flat props
  ├── parse_processing()          → PipelineConfig.processing
  │     └── elements[]            (nvinfer, nvtracker, ...)
  ├── parse_visuals()             → PipelineConfig.visuals
  │     └── elements[]            (tiler, osd, ...)
  ├── parse_outputs()             → PipelineConfig.outputs (vector)
  │     └── per output: elements[]
  └── parse_event_handlers()      → PipelineConfig.event_handlers (vector)
```

```cpp
// infrastructure/config_parser/src/yaml_config_parser.cpp
bool YamlConfigParser::parse(const std::string& file_path,
                             PipelineConfig& config)
{
    YAML::Node doc;
    try {
        doc = YAML::LoadFile(file_path);
    } catch (const YAML::Exception& e) {
        LOG_E("YAML parse error: {}", e.what());
        return false;
    }

    config.version = doc["version"].as<std::string>("");

    parse_pipeline_meta(doc["pipeline"], config.pipeline);
    parse_queue_defaults(doc["queue_defaults"], config.queue_defaults);
    parse_sources(doc["sources"], config.sources);
    parse_processing(doc["processing"], config.processing);
    parse_visuals(doc["visuals"], config.visuals);
    parse_outputs(doc["outputs"], config.outputs);
    parse_event_handlers(doc["event_handlers"], config.event_handlers);

    return true;
}
```

## 7. Environment Variable Substitution

Config hỗ trợ `${}` syntax cho env vars (thực hiện trong parser trước khi parse YAML nodes):

```yaml
sources:
  cameras:
    - id: camera-01
      uri: "${RTSP_URI_CAM01}" # Substituted từ env
```

## 8. Config Validation

```cpp
// ConfigValidator — chạy sau khi parse, trước khi build pipeline:
class ConfigValidator : public engine::core::config::IConfigValidator {
public:
    ValidationResult validate(const PipelineConfig& config) {
        check_sources_has_cameras(config);         // ít nhất 1 camera
        check_batch_size_consistency(config);       // max_batch_size >= cameras.size()
        check_processing_unique_ids(config);        // pgie/sgie unique_id không dupe
        check_operate_on_gie_references(config);    // sgie.operate_on_gie_id phải tồn tại
        check_output_elements_order(config);        // encoder trước sink
        check_event_handler_probe_refs(config);     // probe_element phải tồn tại trong pipeline
        return result_;
    }
};
```
