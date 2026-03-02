# 01. Cấu trúc thư mục chi tiết

## 1. Root Directory

```
vms-engine/
├── CMakeLists.txt          # Root CMake — project setup, deps (vcpkg), subdirs
├── Dockerfile              # Production image (DeepStream runtime + binary)
├── Dockerfile.image        # Base image build (DeepStream SDK)
├── docker-compose.yml      # Dev container orchestration
├── .env / .env.example     # Container env vars (APP_UID, APP_GID, DEEPSTREAM_DIR)
├── README.md               # Quick start guide
└── AGENTS.md               # AI agent context — conventions, build commands
```

## 2. Application Entry (`app/`)

```
app/
├── CMakeLists.txt          # Links vms_engine binary
└── main.cpp                # Entry point
```

### `main.cpp` — Luồng khởi động

```cpp
int main(int argc, char* argv[]) {
    // 1. Parse CLI arguments (-c <config_file>)
    auto config_path = parse_args(argc, argv);

    // 2. Parse YAML config
    engine::infrastructure::config_parser::YamlConfigParser parser;
    auto result = parser.parse(config_path);
    if (!result.ok()) { LOG_C("Config error: {}", result.error()); return 1; }
    const auto& config = result.value();

    // 3. Initialize GStreamer
    gst_init(&argc, &argv);

    // 4. Initialize logger (từ config.pipeline.log_level)
    engine::core::utils::initialize_logger(config);

    // 5. Create GMainLoop
    GMainLoop* loop = g_main_loop_new(nullptr, FALSE);

    // 6. Create & initialize PipelineManager
    auto manager = std::make_unique<engine::pipeline::PipelineManager>();
    if (!manager->initialize(config, loop)) { return 1; }

    // 7. Register event handlers từ config.custom_handlers
    manager->register_event_handlers(config.custom_handlers);

    // 8. Setup signal handlers (SIGINT, SIGTERM → graceful shutdown)
    setup_signal_handlers(manager.get(), loop);

    // 9. Start pipeline
    manager->start();

    // 10. Run main loop (blocks until signal or EOS)
    g_main_loop_run(loop);

    // 11. Cleanup
    manager->stop();
    g_main_loop_unref(loop);
    gst_deinit();
    return 0;
}
```

## 3. Core Layer (`core/`)

> **Quy tắc**: `core/` chỉ phụ thuộc vào C++ standard library và GStreamer forward-declarations. **Không** include DeepStream headers.

```
core/
├── CMakeLists.txt
├── include/engine/core/
│   │
│   ├── builders/                       # Interfaces cho builder system
│   │   ├── ibuilder_factory.hpp        # IBuilderFactory — tạo element builders
│   │   ├── ielement_builder.hpp        # IElementBuilder — build GstElement
│   │   └── ipipeline_builder.hpp       # IPipelineBuilder — orchestrate build
│   │
│   ├── config/                         # Config types (pure data, no logic)
│   │   ├── config_types.hpp            # Root PipelineConfig + tất cả sub-configs
│   │   ├── source_config.hpp           # SourceConfig (nvmultiurisrcbin props)
│   │   ├── muxer_config.hpp            # StreamMuxerConfig (legacy/standalone mux)
│   │   ├── inference_config.hpp        # InferenceConfig (nvinfer / nvinferserver)
│   │   ├── tracker_config.hpp          # TrackerConfig (NvDCF, IOU, DeepSORT)
│   │   ├── analytics_config.hpp        # AnalyticsConfig (ROI, line crossing)
│   │   ├── tiler_config.hpp            # TilerConfig (grid layout)
│   │   ├── osd_config.hpp              # OsdConfig (bbox, text display)
│   │   ├── sink_config.hpp             # OutputConfig (display, file, RTSP)
│   │   ├── encoding_config.hpp         # EncodingConfig (H.264/H.265)
│   │   ├── recording_config.hpp        # SmartRecordConfig
│   │   ├── iconfig_parser.hpp          # IConfigParser interface
│   │   └── iconfig_validator.hpp       # IConfigValidator interface
│   │
│   ├── pipeline/                       # Pipeline lifecycle interface
│   │   ├── ipipeline_manager.hpp       # IPipelineManager
│   │   ├── pipeline_state.hpp          # PipelineState enum
│   │   └── pipeline_info.hpp           # PipelineInfo struct
│   │
│   ├── eventing/                       # Event handling interfaces
│   │   ├── ievent_handler.hpp          # IEventHandler (signal-based)
│   │   ├── ievent_manager.hpp          # IEventManager (registration)
│   │   ├── ievent_listener.hpp         # IEventListener (subscription)
│   │   └── event_types.hpp             # Event type constants
│   │
│   ├── probes/
│   │   └── iprobe_handler.hpp          # IProbeHandler (pad probe)
│   │
│   ├── handlers/
│   │   ├── ihandler.hpp                # Base IHandler interface
│   │   └── handler_registry.hpp        # Plugin handler discovery
│   │
│   ├── messaging/
│   │   ├── imessage_producer.hpp       # IMessageProducer (publish)
│   │   └── imessage_consumer.hpp       # IMessageConsumer (receive)
│   │
│   ├── storage/
│   │   ├── istorage_manager.hpp        # IStorageManager (snapshots)
│   │   └── storage_types.hpp           # StorageTarget, StoragePath
│   │
│   ├── recording/
│   │   ├── ismart_record_controller.hpp # ISmartRecordController
│   │   └── recording_status.hpp        # RecordingStatus enum
│   │
│   ├── runtime/
│   │   ├── iruntime_param_manager.hpp  # IRuntimeParamManager
│   │   └── iruntime_stream_manager.hpp # IRuntimeStreamManager
│   │
│   ├── services/
│   │   └── iexternal_inference_client.hpp # IExternalInferenceClient (Triton)
│   │
│   └── utils/
│       ├── logger.hpp                  # LOG_T/D/I/W/E/C macros
│       ├── spdlog_logger.hpp           # spdlog initialization
│       ├── uuid_v7_generator.hpp       # Time-ordered UUIDv7
│       └── thread_safe_queue.hpp       # Lock-free concurrent queue
│
└── src/
    ├── utils/
    │   ├── spdlog_logger.cpp
    │   └── uuid_v7_generator.cpp
    └── handlers/
        └── handler_registry.cpp
```

## 4. Pipeline Layer (`pipeline/`)

> Đây là nơi chứa toàn bộ **DeepStream-specific implementation**. Tương đương `backends/deepstream/` trong lantanav2 nhưng là layer cấp cao hơn, không phải "backend".

```
pipeline/
├── CMakeLists.txt
├── include/engine/pipeline/
│   │
│   ├── pipeline_manager.hpp            # PipelineManager : IPipelineManager
│   ├── builder_factory.hpp             # BuilderFactory : IBuilderFactory
│   ├── link_manager.hpp                # LinkManager (element connections)
│   ├── queue_manager.hpp               # QueueManager (auto queue insertion)
│   │
│   ├── block_builders/                 # Phase builders (tạo GstBin theo phases)
│   │   ├── base_builder.hpp            # BaseBuilder (abstract base)
│   │   ├── pipeline_builder.hpp        # PipelineBuilder : IPipelineBuilder
│   │   ├── source_builder.hpp          # Phase 1: Sources (nvmultiurisrcbin)
│   │   ├── processing_builder.hpp      # Phase 2: PGIE + SGIE + Tracker + Analytics
│   │   ├── visuals_builder.hpp         # Phase 3: Tiler + OSD
│   │   ├── outputs_builder.hpp         # Phase 4: Encoder + Sinks
│   │   └── standalone_builder.hpp      # Phase 5: Smart Record + MQ Publisher
│   │
│   ├── builders/                       # Element builders (từng GstElement)
│   │   ├── source_builder.hpp          # nvmultiurisrcbin
│   │   ├── muxer_builder.hpp           # nvstreammux (standalone mode)
│   │   ├── infer_builder.hpp           # nvinfer / nvinferserver
│   │   ├── tracker_builder.hpp         # nvtracker
│   │   ├── analytics_builder.hpp       # nvdsanalytics
│   │   ├── demuxer_builder.hpp         # nvstreamdemux
│   │   ├── tiler_builder.hpp           # nvmultistreamtiler
│   │   ├── osd_builder.hpp             # nvdsosd
│   │   ├── encoder_builder.hpp         # nvv4l2h264enc / nvv4l2h265enc
│   │   ├── sink_builder.hpp            # nveglglessink / filesink / rtspclientsink
│   │   ├── smart_record_builder.hpp    # nvdssmartrecordbin
│   │   ├── msgconv_broker_builder.hpp  # nvmsgconv + nvmsgbroker
│   │   └── queue_builder.hpp           # GstQueue element
│   │
│   ├── linking/
│   │   └── pipeline_linker.hpp         # PipelineLinker (static + dynamic pad linking)
│   │
│   ├── probes/                         # GStreamer pad probe implementations
│   │   ├── probe_handler_manager.hpp   # ProbeHandlerManager
│   │   ├── class_id_namespace_handler.hpp  # class_id offset (multi-SGIE)
│   │   ├── crop_object_handler.hpp     # Crop detected objects → storage
│   │   └── smart_record_probe_handler.hpp  # Smart record triggers via probe
│   │
│   ├── event_handlers/                 # Signal-based event handlers
│   │   ├── handler_manager.hpp         # HandlerManager (lifecycle)
│   │   ├── crop_detected_obj_handler.hpp # Crop on appsink
│   │   ├── ext_proc_handler.hpp        # External HTTP processing
│   │   └── smart_record_handler.hpp    # nvdssmartrecordbin signal handler
│   │
│   ├── config/
│   │   └── config_validator.hpp        # ConfigValidator : IConfigValidator
│   │
│   └── services/
│       └── ext_proc_service.hpp        # ExtProcService (HTTP client)
│
└── src/
    ├── pipeline_manager.cpp
    ├── builder_factory.cpp
    ├── link_manager.cpp
    ├── block_builders/
    │   ├── pipeline_builder.cpp
    │   ├── source_builder.cpp
    │   ├── processing_builder.cpp
    │   ├── visuals_builder.cpp
    │   ├── outputs_builder.cpp
    │   └── standalone_builder.cpp
    ├── builders/
    │   ├── source_builder.cpp
    │   ├── muxer_builder.cpp
    │   ├── infer_builder.cpp
    │   ├── tracker_builder.cpp
    │   ├── analytics_builder.cpp
    │   ├── demuxer_builder.cpp
    │   ├── tiler_builder.cpp
    │   ├── osd_builder.cpp
    │   ├── encoder_builder.cpp
    │   ├── sink_builder.cpp
    │   ├── smart_record_builder.cpp
    │   ├── msgconv_broker_builder.cpp
    │   └── queue_builder.cpp
    ├── linking/
    │   └── pipeline_linker.cpp
    ├── probes/
    │   ├── probe_handler_manager.cpp
    │   ├── class_id_namespace_handler.cpp
    │   ├── crop_object_handler.cpp
    │   └── smart_record_probe_handler.cpp
    └── event_handlers/
        ├── handler_manager.cpp
        ├── crop_detected_obj_handler.cpp
        ├── ext_proc_handler.cpp
        └── smart_record_handler.cpp
```

## 5. Domain Layer (`domain/`)

```
domain/
├── CMakeLists.txt
└── include/engine/domain/
    ├── runtime_param_rules.hpp     # Validation rules cho runtime params
    ├── metadata_parser.hpp         # NvDs metadata → domain objects
    └── event_processor.hpp         # Event filtering, dedup, routing
```

## 6. Infrastructure Layer (`infrastructure/`)

```
infrastructure/
├── CMakeLists.txt
├── config_parser/
│   ├── include/engine/infrastructure/config_parser/
│   │   └── yaml_config_parser.hpp      # YamlConfigParser : IConfigParser
│   └── src/
│       ├── yaml_config_parser.cpp      # Main parse() entry
│       ├── yaml_parser_application.cpp # pipeline: section
│       ├── yaml_parser_sources.cpp     # sources: section
│       ├── yaml_parser_processing.cpp  # processing: section
│       ├── yaml_parser_visuals.cpp     # visuals: section
│       ├── yaml_parser_outputs.cpp     # outputs: section
│       ├── yaml_parser_recording.cpp   # smart_record: section
│       ├── yaml_parser_messaging.cpp   # message_broker: section
│       └── yaml_parser_helpers.hpp     # Shared parsing utilities
│
├── messaging/
│   ├── include/engine/infrastructure/messaging/
│   │   ├── redis_stream_producer.hpp   # RedisStreamProducer : IMessageProducer
│   │   └── kafka_adapter.hpp           # KafkaAdapter : IMessageProducer
│   └── src/
│       ├── redis_stream_producer.cpp
│       └── kafka_adapter.cpp
│
├── storage/
│   ├── include/engine/infrastructure/storage/
│   │   ├── local_storage_manager.hpp   # LocalStorageManager : IStorageManager
│   │   └── s3_storage_manager.hpp      # S3StorageManager : IStorageManager
│   └── src/
│       ├── local_storage_manager.cpp
│       └── s3_storage_manager.cpp
│
└── rest_api/
    ├── include/engine/infrastructure/rest_api/
    │   └── pistache_server.hpp         # PistacheServer (runtime HTTP API)
    └── src/
        └── pistache_server.cpp
```

## 7. Services Layer (`services/`)

```
services/
├── CMakeLists.txt
└── triton/
    ├── include/engine/services/
    │   └── triton_inference_client.hpp # TritonClient : IExternalInferenceClient
    └── src/
        └── triton_inference_client.cpp
```

## 8. Configuration Files (`configs/`)

```
configs/
├── default.yml                     # Default multi-source pipeline (reference config)
├── example_single_source.yml       # Single RTSP stream
├── example_multi_camera.yml        # Multi-camera detection
├── example_smart_parking.yml       # Parking analytics config
├── nvinfer/                        # nvinfer TensorRT config files (.txt / .yml)
│   ├── pgie_config.txt
│   ├── pgie_yolo11_config.txt
│   └── sgie_lpr_config.txt
├── tracker/
│   └── nvdcf_config.yml            # NvDCF tracker config
└── analytics/                      # (auto-generated at runtime)
```

> **Tài liệu config YAML đầy đủ** → [`../../configs/deepstream_default.yml`](../../configs/deepstream_default.yml)

## 9. Runtime Data (`dev/`)

```
dev/
└── .gitkeep             # Chỉ file này được track bởi git

# Tạo tự động khi chạy:
dev/
├── logs/
│   ├── app.log                     # spdlog output
│   └── de1_build_graph.dot         # DOT graph (visualize bằng Graphviz)
├── rec/
│   ├── lsr_cam_01_*.mp4            # Smart record clips
│   └── objects/
│       └── *.jpg                   # Cropped object snapshots
└── config/
    └── (generated tracker/analytics configs)
```

### Visualize DOT Graph

```bash
# Sau khi chạy pipeline, export graph nếu dot_file_dir được set:
dot -Tpng dev/logs/de1_build_graph.dot -o pipeline.png
# Hoặc xem trực tiếp với xdot:
xdot dev/logs/de1_build_graph.dot
```

## 10. Build Output (`build/`)

```
build/
├── bin/
│   └── vms_engine           # Main executable
├── lib/
│   ├── libvms_engine_core.a
│   ├── libvms_engine_pipeline.a
│   ├── libvms_engine_domain.a
│   └── libvms_engine_infra.a
└── compile_commands.json    # clangd language server database
```
