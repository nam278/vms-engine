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
config_file_path:  …     →   config-file-path
ll_lib_file:       …     →   ll-lib-file
max_size_buffers:  …     →   max-size-buffers
```

### 2.2 Enum Fields là Integers

Tất cả enum properties trong GStreamer được represent bởi **integer** trong YAML:

```yaml
smart_record: 1          # 0=off, 1=audio+video, 2=video-only
process_mode: 1          # 1=primary (PGIE), 2=secondary (SGIE)
compute_hw: 0            # 0=default, 1=GPU, 2=VIC
```

### 2.3 `queue: {}` Pattern

Thêm `queue: {}` vào bất kỳ element nào để auto-insert GstQueue với default settings trước element đó:

```yaml
- id: "pgie"
  queue: {}              # Insert queue trước pgie

# Hoặc override defaults:
- id: "tracker"
  queue:
    max_size_buffers: 20
    leaky: downstream
```

## 3. Schema Đầy Đủ

```yaml
# ─────────────────────────────────────────────────────────────────────
# VMS Engine Pipeline Configuration
# Version: 1.0.0
# ─────────────────────────────────────────────────────────────────────

pipeline:
  id: "pipeline_main"              # Unique ID
  name: "Main VMS Pipeline"       # Human-readable name
  log_level: "INFO"                # TRACE|DEBUG|INFO|WARN|ERROR|CRITICAL
  gst_log_level: "*:1"            # GStreamer debug categories
  dot_file_dir: "dev/logs"        # Export DOT graph; empty = disabled
  log_file: "dev/logs/app.log"    # spdlog file output; empty = stdout only

# Queue defaults (áp dụng cho queue: {} shorthand)
queue_defaults:
  max_size_buffers: 10
  max_size_bytes_mb: 20
  max_size_time_sec: 0.5
  leaky: "downstream"             # none|upstream|downstream
  silent: true

# ─────────────────────────────────────────────────────────────────────
# SOURCES — nvmultiurisrcbin
# ─────────────────────────────────────────────────────────────────────
sources:
  id: "src_muxer"                  # Element ID trong GStreamer pipeline
  gpu_id: 0

  # Muxer settings (truyền qua nvmultiurisrcbin → nvstreammux)
  width: 1920
  height: 1080
  batched_push_timeout: 40000     # microseconds = 40ms (~25fps)
  live_source: true

  # RTSP/connectivity
  rtsp_reconnect_interval: 10     # seconds; 0 = disable
  cudadec_memtype: 0              # 0=device, 1=pinned, 2=unified

  # Smart record (embedded trong nvmultiurisrcbin)
  smart_record: 0                 # 0=disabled (cấu hình riêng ở phần smart_record)

  # Camera list (URI → thứ tự = stream index)
  cameras:
    - id: "cam_01"
      uri: "rtsp://192.168.1.100:554/stream1"
      name: "Front Gate"
      location: "entrance"

    - id: "cam_02"
      uri: "rtsp://192.168.1.101:554/stream1"
      name: "Parking Lot"

    # Development / testing:
    - id: "cam_test"
      uri: "file:///opt/samples/video.mp4"

# ─────────────────────────────────────────────────────────────────────
# PROCESSING — nvinfer, nvtracker, nvstreamdemux, nvdsanalytics
# ─────────────────────────────────────────────────────────────────────
processing:
  elements:

    # ── Primary Inference (Object Detection) ─────────────────────────
    - id: "pgie"
      role: "primary_inference"
      queue: {}

      type: "nvinfer"              # nvinfer (TensorRT) | nvinferserver (Triton)
      config_file_path: "configs/nvinfer/pgie_config.txt"
      unique_id: 1
      process_mode: 1             # 1 = primary (whole frame)
      batch_size: 4               # Phải match số cameras
      interval: 0                 # Process every N batches; 0 = every batch
      gpu_id: 0

    # ── Tracker ─────────────────────────────────────────────────────
    - id: "nvtracker"
      role: "tracker"
      queue: {}

      ll_lib_file: "/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so"
      ll_config_file: "configs/tracker/nvdcf_config.yml"
      tracker_width: 640
      tracker_height: 384
      gpu_id: 0
      compute_hw: 0
      display_tracking_id: true

    # ── Secondary Inference (LPR — optional) ────────────────────────
    - id: "sgie_lpr"
      role: "secondary_inference"
      queue: {}
      enabled: false              # Set true để bật

      type: "nvinfer"
      config_file_path: "configs/nvinfer/sgie_lpr_config.txt"
      unique_id: 2
      process_mode: 2             # 2 = secondary (per-object)
      batch_size: 16
      operate_on_gie_id: 1        # Chạy trên output của pgie (unique_id=1)
      operate_on_class_ids: "2"   # Chỉ trên class 2 (vehicles)
      gpu_id: 0

    # ── Analytics (optional) ─────────────────────────────────────────
    - id: "analytics"
      role: "analytics"
      queue: {}
      enabled: false

      config_file: "configs/analytics/nvdsanalytics_config.txt"
      gpu_id: 0

    # ── Demuxer ───────────────────────────────────────────────────────
    - id: "demuxer"
      role: "demuxer"
      queue: {}

# ─────────────────────────────────────────────────────────────────────
# VISUALS — nvmultistreamtiler + nvdsosd
# ─────────────────────────────────────────────────────────────────────
visuals:
  enabled: true

  tiler:
    enabled: true
    rows: 2
    columns: 2
    width: 1280
    height: 720
    gpu_id: 0

  osd:
    enabled: true
    process_mode: 0             # 0=CPU, 1=GPU, 2=HW
    display_bbox: true
    display_text: true
    display_mask: false
    border_width: 2
    gpu_id: 0

# ─────────────────────────────────────────────────────────────────────
# OUTPUTS — per stream sinks
# ─────────────────────────────────────────────────────────────────────
outputs:
  - stream_id: "0"              # Tương ứng với stream index từ demuxer
    sinks:

      # Display sink
      - id: "display_0"
        type: "display"         # display|file|rtsp|fake
        sync: false

      # RTSP output
      - id: "rtsp_0"
        type: "rtsp"
        location: "rtsp://localhost:8554/stream0"
        codec: "h264"
        bitrate: 4000000
        iframeinterval: 30
        preset_level: 1

      # File recording (raw — không qua smart record)
      - id: "file_0"
        type: "file"
        location: "dev/rec/output_%05d.mp4"
        codec: "h264"
        bitrate: 4000000

# ─────────────────────────────────────────────────────────────────────
# SMART RECORD (embedded trong nvmultiurisrcbin)
# ─────────────────────────────────────────────────────────────────────
smart_record:
  enabled: true
  mode: 1                      # 1=audio+video, 2=video-only

  # Output directory
  output_dir: "dev/rec"
  file_prefix: "sr_"           # sr_cam01_20240101_120000.mp4

  # Timing
  pre_event_duration_sec: 5    # Buffer trước event (cache trong DTS)
  post_event_duration_sec: 10  # Continue ghi sau event
  default_duration_sec: 30     # Nếu không có stop signal → tự dừng sau N sec

  # Trigger: qua REST API hoặc probe-based auto-trigger
  auto_trigger:
    enabled: false
    class_ids: [0, 2]          # Trigger khi detect class 0 (person) hoặc 2 (vehicle)
    min_confidence: 0.7

# ─────────────────────────────────────────────────────────────────────
# MESSAGE BROKER
# ─────────────────────────────────────────────────────────────────────
message_broker:
  enabled: true

  # nvmsgconv config
  msgconv:
    config: "configs/msgconv_config.txt"
    payload_type: 1            # 0=DEEPSTREAM_SCHEMA, 1=MINIMAL

  # nvmsgbroker
  broker:
    proto_lib: "/opt/nvidia/deepstream/deepstream/lib/libnvds_redis_proto.so"
    conn_str: "localhost;6379;vms_events"
    topic: "vms/events/detections"
    sync: false

    # Alternatives:
    # Kafka:
    # proto_lib: ".../libnvds_kafka_proto.so"
    # conn_str: "localhost;9092"
    # topic: "vms_detections"

# ─────────────────────────────────────────────────────────────────────
# REST API (Pistache HTTP server)
# ─────────────────────────────────────────────────────────────────────
rest_api:
  enabled: false
  host: "0.0.0.0"
  port: 8080
  threads: 4

# ─────────────────────────────────────────────────────────────────────
# STORAGE
# ─────────────────────────────────────────────────────────────────────
storage_configurations:
  - id: "local_storage"
    type: "local"
    base_path: "dev/rec"
    create_subdirs: true

  # S3/MinIO:
  # - id: "s3_storage"
  #   type: "s3"
  #   endpoint: "http://minio:9000"
  #   bucket: "vms-snapshots"
  #   access_key: "${S3_ACCESS_KEY}"
  #   secret_key: "${S3_SECRET_KEY}"

# ─────────────────────────────────────────────────────────────────────
# EXTERNAL SERVICES
# ─────────────────────────────────────────────────────────────────────
external_services:
  - id: "ext_proc"
    type: "http"
    base_url: "http://localhost:8000"
    timeout_ms: 500
    enabled: false

# ─────────────────────────────────────────────────────────────────────
# CUSTOM HANDLERS (signal/probe based)
# ─────────────────────────────────────────────────────────────────────
custom_handlers: []
  # Example:
  # - id: "crop_handler"
  #   type: "crop_detected_obj"
  #   target_element: "tiler_sink_0"
  #   config:
  #     output_dir: "dev/rec/objects"
  #     min_confidence: 0.8
  #     class_ids: [0, 2]
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
  pixelFormat: 3                   # 3=BGR, 1=GRAY

DataAssociator:
  useMatching: 1
```

## 6. YAML Parsing Architecture

```
YamlConfigParser::parse(path)
  ├── parse_pipeline_meta()       → PipelineConfig.pipeline
  ├── parse_queue_defaults()      → PipelineConfig.queue_defaults
  ├── parse_sources()             → PipelineConfig.sources
  ├── parse_processing()          → PipelineConfig.processing (vector)
  ├── parse_visuals()             → PipelineConfig.visuals
  ├── parse_outputs()             → PipelineConfig.outputs (vector)
  ├── parse_smart_record()        → PipelineConfig.smart_record (optional)
  ├── parse_message_broker()      → PipelineConfig.message_broker (optional)
  ├── parse_rest_api()            → PipelineConfig.rest_api (optional)
  ├── parse_storage()             → PipelineConfig.storage_configurations
  ├── parse_external_services()   → PipelineConfig.external_services
  └── parse_custom_handlers()     → PipelineConfig.custom_handlers
```

```cpp
// infrastructure/config_parser/src/yaml_config_parser.cpp
std::expected<PipelineConfig, std::string>
YamlConfigParser::parse(const std::string& file_path) {
    YAML::Node doc;
    try {
        doc = YAML::LoadFile(file_path);
    } catch (const YAML::Exception& e) {
        return std::unexpected(fmt::format("YAML parse error: {}", e.what()));
    }

    PipelineConfig config;
    parse_pipeline_meta(doc, config);
    parse_queue_defaults(doc, config);
    parse_sources(doc, config);
    parse_processing(doc, config);
    parse_visuals(doc, config);
    parse_outputs(doc, config);
    parse_smart_record(doc, config);
    parse_message_broker(doc, config);
    return config;
}
```

## 7. Environment Variable Substitution

Config hỗ trợ `${}` syntax cho env vars (thực hiện trong parser):

```yaml
external_services:
  - id: "s3_storage"
    access_key: "${S3_ACCESS_KEY}"   # Substituted từ env
    secret_key: "${S3_SECRET_KEY}"
```

## 8. Config Validation

```cpp
// ConfigValidator check sau khi parse:
class ConfigValidator : public engine::core::config::IConfigValidator {
public:
    ValidationResult validate(const PipelineConfig& config) {
        check_sources_not_empty(config);
        check_processing_order(config);    // demuxer phải là last
        check_batch_size_consistency(config);
        check_unique_ids_unique(config);   // pgie/sgie unique_id không dupe
        check_operate_on_gie_references(config);  // sgie.operate_on_gie_id phải exist
        check_output_stream_ids(config);
        return result_;
    }
};
```
