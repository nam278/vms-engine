# VMS Engine — Architecture Blueprint

> **Clean Architecture + Builder Pattern + Strategy Pattern for C++ Video Analytics**
> **DeepStream-Native · Config-Driven · Interface-First · Extensible**

---

## 📋 Table of Contents

- [Overview](#overview)
- [Architecture Principles](#architecture-principles)
- [Technology Stack](#technology-stack)
- [Layer Structure](#layer-structure)
- [Directory Structure](#directory-structure)
- [Core Interfaces (Ports)](#core-interfaces-ports)
- [Builder System](#builder-system)
- [Configuration System](#configuration-system)
- [Event & Probe System](#event--probe-system)
- [Infrastructure Adapters](#infrastructure-adapters)
- [Pipeline Lifecycle](#pipeline-lifecycle)
- [Output & Recording System](#output--recording-system)
- [Analytics System](#analytics-system)
- [Design Patterns](#design-patterns)
- [Namespace Convention](#namespace-convention)
- [Build System](#build-system)
- [Migration from lantanav2](#migration-from-lantanav2)
- [Quick Reference](#quick-reference)

---

## Overview

**VMS Engine** is a high-performance Video Management System engine built on **NVIDIA DeepStream SDK 7.1** with **C++17**. It processes real-time video from multiple cameras (RTSP, file, URI) with AI inference (object detection, tracking, analytics) and outputs to display, file recording, RTSP streaming, and message brokers.

### Key Design Goals

- ✅ **Clean Architecture** — Business logic (domain) independent from infrastructure
- ✅ **Interface-First** — Core layer defines contracts; implementations live in dedicated layers
- ✅ **Config-Driven** — Pipeline topology fully described by YAML; zero code changes for new deployments
- ✅ **DeepStream-Native** — Single backend (DeepStream), no unnecessary abstraction layers for multiple backends
- ✅ **Builder Pattern** — Sequential pipeline construction with composable phases
- ✅ **Extensible** — Plugin system for custom event handlers, probes, and external processing
- ✅ **Observable** — Structured logging (spdlog), DOT graph export, GStreamer bus monitoring

### High-Level Architecture

```
┌──────────────────────────────────────────────────────────────────────────┐
│                              main.cpp                                    │
│                         (Application Entry)                              │
└──────────────────────────────┬───────────────────────────────────────────┘
                               │
                               ▼
┌──────────────────────────────────────────────────────────────────────────┐
│                         PipelineManager                                  │
│                    (IPipelineManager impl)                               │
│         - Lifecycle management (init, start, stop, pause)               │
│         - GstBus message handling                                        │
│         - Event handler registration                                     │
└──────────────────────────────┬───────────────────────────────────────────┘
                               │
                               ▼
┌──────────────────────────────────────────────────────────────────────────┐
│                         PipelineBuilder                                  │
│                    (IPipelineBuilder impl)                               │
│              - Orchestrates sub-builders sequentially                    │
│              - Manages element connections via LinkManager               │
└──────────────────────────────┬───────────────────────────────────────────┘
                               │
          ┌────────────────────┼────────────────────┐
          ▼                    ▼                     ▼
┌──────────────────┐ ┌──────────────────┐ ┌──────────────────┐
│  SourceBuilder   │►│ProcessingBuilder │►│  VisualsBuilder  │
│  - Sources       │ │  - PGIE/SGIE     │ │  - Tiler         │
│  - Muxer         │ │  - Tracker       │ │  - OSD           │
│                  │ │  - Analytics     │ │                  │
└──────────────────┘ │  - Demuxer       │ └────────┬─────────┘
                     └──────────────────┘          │
                                                   ▼
                                      ┌──────────────────────┐
                                      │   OutputsBuilder     │
                                      │  - Display sinks     │
                                      │  - File sinks        │
                                      │  - RTSP sinks        │
                                      └────────┬─────────────┘
                                               │
                                               ▼
                                      ┌──────────────────────┐
                                      │  StandaloneBuilder   │
                                      │  - Smart Record      │
                                      │  - Message Broker    │
                                      └──────────────────────┘
```

---

## Architecture Principles

### 1. Dependency Rule — Dependencies Point Inward

Inner layers NEVER depend on outer layers. The core defines interfaces; infrastructure implements them.

```
┌──────────────────────────────────────────┐
│      Presentation / App (main.cpp)       │
│      Orchestrates lifecycle              │
└───────────────┬──────────────────────────┘
                │ depends on
┌───────────────▼──────────────────────────┐
│    Pipeline Layer (Builders, Manager)    │
│    Constructs & manages GStreamer graph  │
└───────────────┬──────────────────────────┘
                │ depends on
┌───────────────▼──────────────────────────┐
│     Core Layer (Interfaces / Ports)      │  ◄── Center (no external deps)
│     IPipelineManager, IBuilderFactory    │
│     IElementBuilder, IProbeHandler       │
│     Config types, Logger                 │
└──────────────────────────────────────────┘
                ▲
                │ implements
┌───────────────┴──────────────────────────┐
│   Infrastructure Layer (Adapters)        │
│   ConfigParser, Messaging, Storage       │
│   REST API, External Services            │
└──────────────────────────────────────────┘
```

### 2. Interface-First Design (Ports & Adapters)

```
          ┌──────────────────────────┐
          │       Core Ports         │
          │  (Abstract Interfaces)   │
          └────────┬─────┬───────────┘
                   │     │
     ┌─────────────┘     └─────────────┐
     ▼                                 ▼
┌──────────────┐              ┌──────────────────┐
│  Driving     │              │  Driven          │
│  Adapters    │              │  Adapters        │
│ (Input)      │              │ (Output)         │
├──────────────┤              ├──────────────────┤
│ main.cpp     │              │ YAML Parser      │
│ REST API     │              │ Redis Producer   │
│ CLI args     │              │ Kafka Adapter    │
│              │              │ S3 Storage       │
│              │              │ Local Storage    │
└──────────────┘              └──────────────────┘
```

**Key Concepts:**

- **Ports** (Interfaces): `IPipelineManager`, `IBuilderFactory`, `IElementBuilder`, `IProbeHandler`, `IStorageManager`, `IMessageProducer`, `IConfigParser`
- **Driving Adapters** (Input): `main.cpp`, REST API — push commands into the system
- **Driven Adapters** (Output): Redis, Kafka, S3, Local FS — system pushes data out

### 3. Single Backend — No Unnecessary Abstraction

Unlike lantanav2 which abstracted over DeepStream/DLStreamer, vms-engine is **DeepStream-native**. Config types can reference DeepStream-specific options directly without `std::variant` wrappers. This reduces complexity without sacrificing clean architecture — the interface-first design still allows swapping implementations if needed in the future.

### 4. Config-Driven Pipeline Construction

Pipeline topology is 100% defined by a YAML file. Zero code changes needed for:

- Adding/removing cameras
- Changing inference models
- Enabling/disabling analytics
- Switching output modes (display, file, RTSP)
- Configuring event handlers and processing rules

---

## Technology Stack

| Component           | Technology                  | Purpose                               |
| ------------------- | --------------------------- | ------------------------------------- |
| **Language**        | C++17                       | Performance-critical video processing |
| **Build System**    | CMake 3.16+ + vcpkg         | Cross-platform build with pkg mgmt    |
| **Video Framework** | GStreamer 1.0               | Pipeline-based multimedia framework   |
| **AI Backend**      | NVIDIA DeepStream SDK 7.1   | GPU-accelerated video analytics       |
| **GPU Inference**   | TensorRT, CUDA              | Model optimization & GPU execution    |
| **Configuration**   | YAML (yaml-cpp)             | Human-readable pipeline config        |
| **Logging**         | spdlog + fmt                | Structured, leveled logging           |
| **Messaging**       | Redis Streams, Kafka        | Event publishing to external systems  |
| **Storage**         | Local FS, S3 (MinIO)        | Snapshot/recording persistence        |
| **REST API**        | Pistache (lightweight HTTP) | Runtime control & monitoring          |
| **Ext. Inference**  | Triton Inference Client     | External model serving integration    |

---

## Layer Structure

### Layer Overview

```
vms-engine/
├── app/                     # Application Layer — Entry point, wiring
├── core/                    # Core Layer — Interfaces, config types, utilities
│   ├── builders/            #   Builder interfaces (IPipelineBuilder, IElementBuilder...)
│   ├── config/              #   Configuration data types (value objects)
│   ├── pipeline/            #   Pipeline management interface (IPipelineManager)
│   ├── eventing/            #   event_types.hpp — event name constants (ON_EOS, ON_DETECT, …)
│   ├── probes/              #   Probe handler interface (IProbeHandler)
│   ├── messaging/           #   Message producer/consumer interfaces
│   ├── storage/             #   Storage manager interface
│   ├── runtime/             #   Runtime param/stream manager interfaces
│   └── utils/               #   Logger, UUID, thread-safe queue
├── pipeline/                # Pipeline Layer — DeepStream builder implementations
│   ├── builders/            #   Element builders (source, infer, tracker, sink...)
│   ├── block_builders/      #   Phase builders (SourceBuilder, ProcessingBuilder...)
│   ├── linking/             #   Element linking & tee management
│   ├── probes/              #   GStreamer probe implementations (SmartRecord, CropObjects)
│   ├── config/              #   Config validation
├── domain/                  # Domain Layer — Business rules & metadata processing
│   ├── runtime_params/      #   Runtime parameter rules
│   ├── metadata/            #   Metadata parsing & enrichment
│   └── event_processing/    #   Event filtering, dedup, routing logic
├── infrastructure/          # Infrastructure Layer — Technical adapters
│   ├── config_parser/       #   YAML config parser implementation
│   ├── messaging/           #   Redis, Kafka message producers
│   ├── storage/             #   Local FS, S3 storage implementations
│   └── rest_api/            #   HTTP API server (Pistache)
├── plugins/                 # Plugins — Runtime-loadable .so handlers
├── configs/                 # Configuration Files — YAML pipeline configs
├── dev/                     # Runtime data dir — git-ignored (only .gitkeep tracked)
├── docs/                    # Documentation
└── scripts/                 # Build, deploy, utility scripts
```

### Layer Dependency Rules

| Layer               | Can Depend On                             | Cannot Depend On             |
| ------------------- | ----------------------------------------- | ---------------------------- |
| **app/**            | core, pipeline, infrastructure            | ∅                            |
| **core/**           | ∅ (only std lib + GStreamer fwd-declares) | pipeline, infra              |
| **pipeline/**       | core                                      | infra (directly)             |
| **domain/**         | core                                      | pipeline, infra              |
| **infrastructure/** | core                                      | pipeline, domain             |
| **plugins/**        | core                                      | pipeline (linked at runtime) |

---

## Directory Structure

### Detailed File Layout

```
vms-engine/
├── CMakeLists.txt                          # Root CMake — project, deps, subdirs
├── Dockerfile                              # Production container
├── Dockerfile.image                        # Base image with DeepStream SDK
├── docker-compose.yml                      # Dev orchestration
├── .env.example                            # Environment variables template
├── README.md                               # Quick start guide
│
├── app/                                    # ═══ APPLICATION LAYER ═══
│   ├── CMakeLists.txt                      # App build config
│   └── main.cpp                            # Entry point: parse → init → run → cleanup
│
├── core/                                   # ═══ CORE LAYER (Interfaces & Types) ═══
│   ├── CMakeLists.txt                      # Core library build
│   ├── include/
│   │   └── engine/core/                    # Namespace: engine::core::*
│   │       ├── builders/
│   │       │   ├── ibuilder_factory.hpp    # Factory for creating element builders
│   │       │   ├── ielement_builder.hpp    # Interface for individual element builders
│   │       │   └── ipipeline_builder.hpp   # Interface for pipeline orchestrator
│   │       ├── config/
│   │       │   ├── config_types.hpp        # Root PipelineConfig + all sub-configs
│   │       │   ├── source_config.hpp       # SourceConfig, SourceBackendOptions
│   │       │   ├── muxer_config.hpp        # StreamMuxerConfig
│   │       │   ├── inference_config.hpp    # InferenceConfig (nvinfer options)
│   │       │   ├── tracker_config.hpp      # TrackerConfig (NvDCF, IOU)
│   │       │   ├── analytics_config.hpp    # AnalyticsConfig (ROI, line crossing)
│   │       │   ├── tiler_config.hpp        # TilerConfig
│   │       │   ├── osd_config.hpp          # OsdConfig
│   │       │   ├── sink_config.hpp         # OutputConfig (display, file, RTSP)
│   │       │   ├── encoding_config.hpp     # EncodingConfig (h264/h265)
│   │       │   ├── recording_config.hpp    # SmartRecordConfig
│   │       │   ├── iconfig_parser.hpp      # IConfigParser interface
│   │       │   └── iconfig_validator.hpp   # IConfigValidator interface
│   │       ├── pipeline/
│   │       │   ├── ipipeline_manager.hpp   # Pipeline lifecycle interface
│   │       │   ├── pipeline_state.hpp      # PipelineState enum
│   │       │   └── pipeline_info.hpp       # PipelineInfo struct
│   │       ├── eventing/
│   │       │   └── event_types.hpp         # Event name constants (ON_EOS, ON_DETECT, …)
│   │       ├── probes/
│   │       │   └── iprobe_handler.hpp      # IProbeHandler (pad probe)
│   │       ├── messaging/
│   │       │   ├── imessage_producer.hpp   # IMessageProducer (publish events)
│   │       │   └── imessage_consumer.hpp   # IMessageConsumer (receive commands)
│   │       ├── storage/
│   │       │   ├── istorage_manager.hpp    # IStorageManager (save snapshots)
│   │       │   └── storage_types.hpp       # StorageTarget, StoragePath types
│   │       ├── runtime/
│   │       │   ├── iruntime_param_manager.hpp   # IRuntimeParamManager
│   │       │   └── iruntime_stream_manager.hpp  # IRuntimeStreamManager
│   │       └── utils/
│   │           ├── logger.hpp              # LOG_T/D/I/W/E/C macros
│   │           ├── spdlog_logger.hpp       # spdlog initialization
│   │           ├── uuid_v7_generator.hpp   # Time-ordered UUID generation
│   │           └── thread_safe_queue.hpp   # Lock-free concurrent queue
│   └── src/
│       ├── utils/
│       │   ├── spdlog_logger.cpp
│       │   └── uuid_v7_generator.cpp
│       └── handlers/
│           └── handler_registry.cpp
│
├── pipeline/                               # ═══ PIPELINE LAYER (DeepStream) ═══
│   ├── CMakeLists.txt
│   ├── include/
│   │   └── engine/pipeline/                # Namespace: engine::pipeline::*
│   │       ├── pipeline_manager.hpp        # PipelineManager : IPipelineManager
│   │       ├── builder_factory.hpp         # BuilderFactory : IBuilderFactory
│   │       ├── link_manager.hpp            # LinkManager (element connections)
│   │       ├── queue_manager.hpp           # QueueManager (auto queue insertion)
│   │       │
│   │       ├── block_builders/             # Phase builders (GstBin orchestration)
│   │       │   ├── base_builder.hpp        # BaseBuilder (abstract base)
│   │       │   ├── pipeline_builder.hpp    # PipelineBuilder : IPipelineBuilder
│   │       │   ├── source_builder.hpp      # Phase 1: Sources + Muxer
│   │       │   ├── processing_builder.hpp  # Phase 2: PGIE/SGIE + Tracker + Analytics
│   │       │   ├── visuals_builder.hpp     # Phase 3: Tiler + OSD
│   │       │   ├── outputs_builder.hpp     # Phase 4: Display/File/RTSP sinks
│   │       │   └── standalone_builder.hpp  # Phase 5: Smart Record + MQ Publisher
│   │       │
│   │       ├── builders/                   # Element builders (individual GstElement)
│   │       │   ├── source_builder.hpp      # nvurisrcbin / nvmultiurisrcbin
│   │       │   ├── muxer_builder.hpp       # nvstreammux
│   │       │   ├── infer_builder.hpp       # nvinfer (PGIE/SGIE)
│   │       │   ├── tracker_builder.hpp     # nvtracker
│   │       │   ├── analytics_builder.hpp   # nvdsanalytics
│   │       │   ├── demuxer_builder.hpp     # nvstreamdemux
│   │       │   ├── tiler_builder.hpp       # nvmultistreamtiler
│   │       │   ├── osd_builder.hpp         # nvdsosd
│   │       │   ├── encoder_builder.hpp     # nvv4l2h264enc / nvv4l2h265enc
│   │       │   ├── sink_builder.hpp        # nveglglessink / filesink / rtspclientsink
│   │       │   ├── smart_record_builder.hpp # nvdssmartrecordbin
│   │       │   ├── msgconv_broker_builder.hpp # nvmsgconv + nvmsgbroker
│   │       │   └── queue_builder.hpp       # queue elements
│   │       │
│   │       ├── probes/                     # GStreamer pad probe implementations
│   │       │   ├── probe_handler_manager.hpp     # ProbeHandlerManager
│   │       │   ├── class_id_namespace_handler.hpp # class_id offset/restore
│   │       │   ├── crop_object_handler.hpp       # Object crop capture
│   │       │   └── smart_record_probe_handler.hpp # Smart record triggers
│   │       │
│   │       ├── config/
│   │       │   └── config_validator.hpp    # ConfigValidator : IConfigValidator
│   │       │
│   └── src/
│       ├── pipeline_manager.cpp
│       ├── builder_factory.cpp
│       ├── link_manager.cpp
│       ├── block_builders/
│       │   ├── pipeline_builder.cpp
│       │   ├── source_builder.cpp
│       │   ├── processing_builder.cpp
│       │   ├── visuals_builder.cpp
│       │   ├── outputs_builder.cpp
│       │   └── standalone_builder.cpp
│       ├── builders/
│       │   ├── source_builder.cpp
│       │   ├── muxer_builder.cpp
│       │   ├── infer_builder.cpp
│       │   ├── tracker_builder.cpp
│       │   ├── analytics_builder.cpp
│       │   ├── demuxer_builder.cpp
│       │   ├── tiler_builder.cpp
│       │   ├── osd_builder.cpp
│       │   ├── encoder_builder.cpp
│       │   ├── sink_builder.cpp
│       │   ├── smart_record_builder.cpp
│       │   ├── msgconv_broker_builder.cpp
│       │   └── queue_builder.cpp
│       ├── probes/
│       │   ├── probe_handler_manager.cpp
│       │   ├── class_id_namespace_handler.cpp
│       │   ├── crop_object_handler.cpp
│       │   └── smart_record_probe_handler.cpp
│
├── domain/                                 # ═══ DOMAIN LAYER (Business Rules) ═══
│   ├── CMakeLists.txt
│   ├── include/
│   │   └── engine/domain/                  # Namespace: engine::domain::*
│   │       ├── runtime_param_rules.hpp     # Runtime parameter validation rules
│   │       ├── metadata_parser.hpp         # NvDs metadata → domain objects
│   │       └── event_processor.hpp         # Event filtering, dedup, routing
│   └── src/
│       ├── runtime_param_rules.cpp
│       ├── metadata_parser.cpp
│       └── event_processor.cpp
│
├── infrastructure/                         # ═══ INFRASTRUCTURE LAYER (Adapters) ═══
│   ├── CMakeLists.txt
│   ├── config_parser/
│   │   ├── include/
│   │   │   └── engine/infrastructure/config_parser/
│   │   │       └── yaml_config_parser.hpp  # YamlConfigParser : IConfigParser
│   │   └── src/
│   │       ├── yaml_config_parser.cpp      # Main parser entry
│   │       ├── yaml_parser_application.cpp # application: section
│   │       ├── yaml_parser_sources.cpp     # sources: section
│   │       ├── yaml_parser_muxer.cpp       # stream_muxer: section
│   │       ├── yaml_parser_processing.cpp  # processing_flow: section
│   │       ├── yaml_parser_analytics.cpp   # analytics: section
│   │       ├── yaml_parser_visuals.cpp     # visuals: section
│   │       ├── yaml_parser_outputs.cpp     # outputs: section
│   │       ├── yaml_parser_recording.cpp   # smart_record: section
│   │       ├── yaml_parser_messaging.cpp   # message_queue_publisher: section
│   │       ├── yaml_parser_storage.cpp     # storage_configurations: section
│   │       ├── yaml_parser_handlers.cpp    # custom_handlers: section
│   │       ├── yaml_parser_api.cpp         # rest_api: section
│   │       └── yaml_parser_helpers.hpp     # Shared parsing utilities
│   ├── messaging/
│   │   ├── include/
│   │   │   └── engine/infrastructure/messaging/
│   │   │       ├── redis_stream_producer.hpp  # RedisStreamProducer : IMessageProducer
│   │   │       └── kafka_adapter.hpp          # KafkaAdapter : IMessageProducer
│   │   └── src/
│   │       ├── redis_stream_producer.cpp
│   │       └── kafka_adapter.cpp
│   ├── storage/
│   │   ├── include/
│   │   │   └── engine/infrastructure/storage/
│   │   │       ├── local_storage_manager.hpp  # LocalStorageManager : IStorageManager
│   │   │       └── s3_storage_manager.hpp     # S3StorageManager : IStorageManager
│   │   └── src/
│   │       ├── local_storage_manager.cpp
│   │       └── s3_storage_manager.cpp
│   └── rest_api/
│       ├── include/
│       │   └── engine/infrastructure/rest_api/
│       │       └── pistache_server.hpp        # PistacheServer (runtime HTTP API)
│       └── src/
│           └── pistache_server.cpp
│
├── plugins/                                # ═══ PLUGIN SYSTEM ═══
│   ├── CMakeLists.txt
│   └── src/
│       └── (custom handler .so plugins)
│
├── configs/                                # ═══ CONFIGURATION FILES ═══
│   ├── default.yml                         # Default multi-source config
│   ├── example_single_source.yml           # Simple single-stream example
│   ├── example_multi_camera.yml            # Multi-camera detection
│   ├── example_smart_parking.yml           # Parking analytics
│   ├── nvinfer/                            # nvinfer config files
│   │   ├── pgie_config.txt
│   │   ├── sgie_lpr_config.txt
│   │   └── pgie_yolo11_config.txt
│   ├── tracker/                            # Tracker config
│   │   └── nvdcf_config.yml
│   └── analytics/                          # Auto-generated at runtime
│
├── dev/                                    # ═══ RUNTIME DATA DIR ═══
│   └── .gitkeep                            # Tracked; all other contents git-ignored
│   # Subdirs created at runtime (examples):
│   # ├── logs/          pipeline logs, DOT graphs
│   # ├── rec/           smart record clips
│   # ├── rec/objects/   cropped object snapshots
│   # └── config/        generated tracker/analytics configs
│
├── docs/                                   # ═══ DOCUMENTATION ═══
│   ├── architecture/
│   │   └── ARCHITECTURE_BLUEPRINT.md       # This file
│   └── plans/
│       └── phase1_refactor/
│
└── scripts/                                # ═══ SCRIPTS ═══
    ├── build.sh
    └── deploy.sh
```

---

## Core Interfaces (Ports)

The core layer defines **pure abstract classes** (interfaces) that serve as contracts between layers. No implementation details leak into the core.

### IPipelineManager — Pipeline Lifecycle

```cpp
// core/include/engine/core/pipeline/ipipeline_manager.hpp
namespace engine::core::pipeline {

class IPipelineManager {
public:
    virtual ~IPipelineManager() = default;

    // Lifecycle
    virtual bool initialize(PipelineConfig& config,
                           GMainLoop* main_loop_context = nullptr) = 0;
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual bool pause() = 0;

    // State query
    virtual PipelineState get_state() const = 0;
    virtual PipelineInfo get_info() const = 0;
    virtual GstElement* get_gst_pipeline() const = 0;
};

} // namespace engine::core::pipeline
```

### IPipelineBuilder — Pipeline Construction

```cpp
// core/include/engine/core/builders/ipipeline_builder.hpp
namespace engine::core::builders {

class IPipelineBuilder {
public:
    virtual ~IPipelineBuilder() = default;

    /// Build the GStreamer pipeline from full configuration.
    /// Returns true on success; access the resulting element via get_pipeline().
    virtual bool build(const engine::core::config::PipelineConfig& config,
                       GMainLoop* main_loop) = 0;

    virtual GstElement* get_pipeline() const = 0;
};

} // namespace engine::core::builders
```

### IBuilderFactory — Element Builder Creation

The factory **creates typed builder instances** only — it does NOT slice config into
the builder at creation time. Builders receive the full `PipelineConfig` when their
`build()` method is called (Full Config Pattern, see below).

```cpp
// core/include/engine/core/builders/ibuilder_factory.hpp
namespace engine::core::builders {

class IBuilderFactory {
public:
    virtual ~IBuilderFactory() = default;

    /// Create a builder for the sources block (nvmultiurisrcbin).
    virtual std::unique_ptr<IElementBuilder> create_source_builder() = 0;

    /// Create a builder for a processing element identified by role.
    /// role: "primary_inference" | "secondary_inference" | "tracker" | "demuxer"
    virtual std::unique_ptr<IElementBuilder> create_processing_builder(
        const std::string& role) = 0;

    /// Create a builder for a visuals element identified by role.
    /// role: "tiler" | "osd"
    virtual std::unique_ptr<IElementBuilder> create_visual_builder(
        const std::string& role) = 0;

    /// Create a builder for an output sink identified by type.
    /// type: "rtsp_client" | "filesink" | "appsink" | "display"
    virtual std::unique_ptr<IElementBuilder> create_output_builder(
        const std::string& type) = 0;
};

} // namespace engine::core::builders
```

### IElementBuilder — Individual Element Construction

```cpp
// core/include/engine/core/builders/ielement_builder.hpp
namespace engine::core::builders {

class IElementBuilder {
public:
    virtual ~IElementBuilder() = default;

    /// Build a GstElement or GstBin.
    /// Receives the FULL PipelineConfig — the builder extracts its own
    /// relevant section(s) by type and `index`.
    /// `index` is meaningful for repeated sections (e.g. outputs[index],
    /// processing.elements[index]); ignored for single-block sections.
    virtual GstElement* build(const engine::core::config::PipelineConfig& config,
                              int index = 0) = 0;
};

} // namespace engine::core::builders
```

### IProbeHandler — Pad Probe Processing

```cpp
// core/include/engine/core/probes/iprobe_handler.hpp
namespace engine::core::probes {

class IProbeHandler {
public:
    virtual ~IProbeHandler() = default;

    virtual GstPadProbeReturn process_probe(
        GstPad* pad, GstPadProbeInfo* info,
        const std::string& event_type,
        std::any* batch_meta, std::any* frame_meta,
        std::any* object_meta, std::any* analytics_meta) = 0;
};

} // namespace engine::core::probes
```

### Infrastructure Interfaces

```cpp
// core/include/engine/core/messaging/imessage_producer.hpp
namespace engine::core::messaging {
class IMessageProducer {
public:
    virtual ~IMessageProducer() = default;
    virtual bool connect(const std::string& host, int port) = 0;
    virtual bool publish(const std::string& channel, const std::string& message) = 0;
    virtual void disconnect() = 0;
};
}

// core/include/engine/core/storage/istorage_manager.hpp
namespace engine::core::storage {
class IStorageManager {
public:
    virtual ~IStorageManager() = default;
    virtual bool save(const std::string& path, const void* data, size_t size) = 0;
    virtual bool save_file(const std::string& src_path, const std::string& dest_path) = 0;
    virtual std::string get_url(const std::string& path) = 0;
};
}

// core/include/engine/core/config/iconfig_parser.hpp
namespace engine::core::config {
using ParseResult = std::variant<PipelineConfig, std::string>;
class IConfigParser {
public:
    virtual ~IConfigParser() = default;
    virtual ParseResult parse(const std::string& config_file_path) = 0;
};
}
```

### Interface Relationship Diagram

```
                    ┌─────────────────────┐
                    │  IPipelineManager   │
                    │  - initialize()     │
                    │  - start/stop()     │
                    └─────────┬───────────┘
                              │ uses
                              ▼
                    ┌─────────────────────┐
                    │  IPipelineBuilder   │
                    │  - build_pipeline() │
                    └─────────┬───────────┘
                              │ uses
                              ▼
                    ┌─────────────────────┐
                    │  IBuilderFactory    │
                    │  - create_*_builder │
                    └─────────┬───────────┘
                              │ creates
                              ▼
                    ┌─────────────────────┐
                    │  IElementBuilder    │──────────── builds ──▶ GstElement*
                    └─────────────────────┘

              ┌─────────────────────┐
              │    IProbeHandler    │  (pad probe — no base class)
              └─────────────────────┘

    ┌──────────────┬──────────────┬──────────────┐
    │IMessageProdr │IStorageMgr   │IConfigParser │
    └──────────────┘──────────────┘──────────────┘
```

---

## Builder System

### 5-Phase Pipeline Construction

The pipeline is built in **5 sequential phases**. Each phase is a `BaseBuilder` subclass that:

1. Creates a `GstBin` containing related elements
2. Uses `IBuilderFactory` to create element builders
3. Links elements within its bin
4. Registers upstream endpoint in `tails_` map for next phase

```
Phase 1: SourceBuilder       Phase 2: ProcessingBuilder     Phase 3: VisualsBuilder
┌───────────────────┐        ┌────────────────────────┐     ┌──────────────────┐
│ nvurisrcbin       │        │ nvinfer (PGIE)         │     │ tee              │
│ nvmultiurisrcbin  │───────►│ nvtracker              │────►│ nvmultistreamtlr │
│ nvstreammux       │        │ nvinfer (SGIE)         │     │ nvdsosd          │
└───────────────────┘        │ nvdsanalytics          │     └────────┬─────────┘
                             │ nvstreamdemux          │              │
                             └────────────────────────┘              ▼
Phase 5: StandaloneBuilder   Phase 4: OutputsBuilder
┌───────────────────┐        ┌────────────────────────┐
│ nvdssmartrecordbin│◄───────│ nveglglessink          │
│ nvmsgconv         │        │ filesink + encoder     │
│ nvmsgbroker       │        │ rtspclientsink         │
└───────────────────┘        └────────────────────────┘
```

### BaseBuilder — Abstract Base

```cpp
// pipeline/include/engine/pipeline/block_builders/base_builder.hpp
namespace engine::pipeline::block_builders {

class BaseBuilder {
protected:
    GstElement* pipeline_;
    std::shared_ptr<IBuilderFactory> factory_;
    const PipelineConfig* config_;
    std::map<std::string, GstElement*> elements_;
    std::shared_ptr<LinkManager> link_manager_;

public:
    BaseBuilder(GstElement* pipeline,
                std::shared_ptr<IBuilderFactory> factory,
                const PipelineConfig* config,
                std::shared_ptr<LinkManager> link_manager);
    virtual ~BaseBuilder() = default;

    // ═══ Abstract methods ═══
    virtual void build() = 0;
    virtual std::string get_common_upstream_id() const = 0;
    virtual GstElement* get_bin() const = 0;

protected:
    // Helpers
    bool add_element_to_bin(GstElement* bin, GstElement* element, const std::string& id);
    static void expose_ghost_pads(GstElement* bin, GstElement* first, GstElement* last,
                                  const std::string& bin_name);
    std::pair<std::string, std::optional<std::string>>
        parse_input_source(const std::string& input_from);
    bool is_dynamic_pad_provider(GstElement* element);
    void ensure_pad_added_signal(GstElement* element, const std::string& id);
};

} // namespace engine::pipeline::block_builders
```

### tails\_ Map — Phase Endpoint Tracking

The `tails_` map tracks the **last GstBin** of each phase, allowing the next phase to know where to connect.
Each block builder wraps its elements in a `GstBin` with ghost pads. The bin pointer is stored under the key `"src"`:

```cpp
std::map<std::string, GstElement*> tails_;  // value is GstBin* (cast to GstElement*)

// After SourceBlockBuilder:   tails_["src"] = sources_bin    (GstBin*)
// After ProcessingBlockBuilder: tails_["src"] = processing_bin (GstBin*)
// After VisualsBlockBuilder:  tails_["src"] = visuals_bin   (GstBin*)
// Each bin exposes ghost "src" pad → gst_element_link(bin_a, bin_b) works naturally
```

### PipelineBuilder — Orchestrator

```cpp
// Simplified build flow
GstElement* PipelineBuilder::build_pipeline(const PipelineConfig& config,
                                            std::shared_ptr<IBuilderFactory> factory) {
    pipeline_ = gst_pipeline_new(config.application.name.c_str());
    std::string upstream_id;

    // Phase 1: Sources
    SourceBuilder source_builder(pipeline_, factory, &config, link_manager_);
    source_builder.build();
    upstream_id = source_builder.get_common_upstream_id();
    tails_[upstream_id] = source_builder.get_bin();

    // Phase 2: Processing Flow (PGIE → Tracker → SGIE → Analytics → Demuxer)
    ProcessingBuilder proc_builder(pipeline_, factory, &config, link_manager_, upstream_id, tails_);
    proc_builder.build();
    upstream_id = proc_builder.get_common_upstream_id();

    // Phase 3: Visuals (Tiler → OSD)
    VisualsBuilder vis_builder(pipeline_, factory, &config, link_manager_, upstream_id, tails_);
    vis_builder.build();
    upstream_id = vis_builder.get_common_upstream_id();

    // Phase 4: Outputs (Display / File / RTSP)
    OutputsBuilder out_builder(pipeline_, factory, &config, link_manager_, upstream_id, tails_);
    out_builder.build();

    // Phase 5: Standalone (Smart Record, Message Queue)
    StandaloneBuilder sa_builder(pipeline_, factory, &config, link_manager_, tails_);
    sa_builder.build();

    return pipeline_;
}
```

### Linking System

```
┌─────────────────────────────────────────────────────────────────────┐
│                          Block Builders                              │
│              (SourceBuilder, ProcessingBuilder, etc.)                │
└───────────────────────────────┬─────────────────────────────────────┘
                                │ uses
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                           LinkManager                                │
│  - link_elements()           Basic static pad linking               │
│  - link_with_tee()           Single-branch tee                      │
│  - link_with_tee_multi()     Multi-branch tee setup                 │
│  - register_pending_link()   Dynamic pad handling (pad-added)       │
└───────────────────────────────┬─────────────────────────────────────┘
                                │ uses
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                          QueueManager                                │
│  - needs_queue_before()      Check if element needs queue           │
│  - create_queue()            Create configured queue element        │
└─────────────────────────────────────────────────────────────────────┘
```

**Tee Branching Topology:**

```
src ──► queue ──► tee ──► queue ──► sink1
                     ├──► queue ──► sink2
                     └──► queue ──► sink3
```

### Full Config Pattern

All element builders receive the **complete `PipelineConfig`** by const-ref. No config
slice is passed at construction time. Each builder extracts only the section it needs.

**Rationale:** Cross-section access requirements arise naturally.
`SourceBuilder` needs `queue_defaults`; `OutputsBuilder` needs both `outputs[i]`
and `queue_defaults`. Pre-slicing forces the caller to predict every dependency;
full config makes any cross-section read free and zero-cost.

```cpp
// Every builder follows this contract:
class SourceBuilder : public IElementBuilder {
public:
    GstElement* build(const engine::core::config::PipelineConfig& config,
                      int /*index*/ = 0) override {
        const auto& src = config.sources;
        const auto& q   = config.queue_defaults;  // cross-section — free
        // config.pipeline.id available for element naming, etc.
    }
};

class OutputsBuilder : public IElementBuilder {
public:
    GstElement* build(const engine::core::config::PipelineConfig& config,
                      int index = 0) override {
        const auto& out = config.outputs[index];  // per-output via index
        const auto& q   = config.queue_defaults;  // shared queue defaults
    }
};

class ProcessingBuilder : public IElementBuilder {
public:
    GstElement* build(const engine::core::config::PipelineConfig& config,
                      int index = 0) override {
        const auto& elem = config.processing.elements[index];
        // config.pipeline.id for unique GstElement naming
    }
};
```

**Rules:**

- `config` is **always `const PipelineConfig&`** — read-only; no side effects.
- `index = 0` for single-instance builders (sources, visuals blocks).
- `index > 0` meaningful for repeated sections (outputs, processing.elements).
- Builders **must not** store a pointer/reference to `config` beyond the `build()` call.

---

## Configuration System

### YAML Root Structure

The YAML schema is structured to **mirror GStreamer pipeline topology** — reading
top-to-bottom corresponds to the pipeline left-to-right. Each stage is a named
top-level block; nested `queue: {}` declares a `GstQueue` inline before its element.

```yaml
version: "1.0.0"

pipeline: # Pipeline metadata — id, name, log settings
queue_defaults: # Default GstQueue params; any `queue: {}` inherits these
sources: # Single block → nvmultiurisrcbin (cameras[], smart_record)
processing: # elements: [] list — nvinfer, nvtracker, nvstreamdemux
visuals: # elements: [] list — nvmultistreamtiler, nvdsosd
outputs: # [] list of output sinks (rtsp_client, filesink, appsink)
event_handlers: # [] probe/signal-based callbacks (smart_record, crop_object)
```

**Queue Semantics:**

```yaml
# queue: {}         → insert GstQueue here; inherit all fields from queue_defaults
# queue: { ... }   → insert GstQueue here; override only specified fields
# (no queue field) → no GstQueue before this element

# leaky semantics (GStreamer GstQueue):
#   downstream (=2): drop OLDEST buffer already in queue when full
#                    → newest frame always gets in  ← USE THIS for realtime AI / live streams
#   upstream   (=1): drop NEWEST incoming buffer when full
#                    → old frames kept, new ones silently lost at input
#   no         (=0): block upstream until space available
#                    → causes pipeline stall / latency spike on live streams, avoid
```

### PipelineConfig — Root C++ Struct

```cpp
// core/include/engine/core/config/config_types.hpp
namespace engine::core::config {

struct QueueConfig {
    int  max_size_buffers = 10;
    int  max_size_bytes_mb = 20;
    float max_size_time_sec = 0.5f;
    // 2 (downstream): drop OLDEST in queue → newest frame always enters  ← USE for realtime
    // 1 (upstream):   drop NEWEST incoming → old frames kept, new ones lost
    // 0 (none):       block upstream until space → causes pipeline stall on live streams
    int leaky = 2;
    bool silent = true;
};

struct PipelineConfig {
    std::string       version = "1.0.0";
    PipelineMetaConfig pipeline;     // id, name, log settings
    QueueConfig        queue_defaults;

    SourcesConfig         sources;   // single block (not vector)
    ProcessingConfig      processing;
    VisualsConfig         visuals;
    std::vector<OutputConfig>       outputs;
    std::vector<EventHandlerConfig> event_handlers;

    // Optional infrastructure config
    std::optional<RestApiConfig>    rest_api;
    std::vector<BrokerConfig>       broker_configurations;
    std::vector<StorageTargetConfig> storage_configurations;
};

} // namespace engine::core::config
```

### Key Configuration Sections

#### Sources

Single top-level block — not an array. Maps to `nvmultiurisrcbin`.
Cameras are listed as `cameras: [{name, uri}]` so name and URI are co-located.
`smart_record` is a property of this block because it IS a property of `nvmultiurisrcbin`.

```yaml
sources:
  type: nvmultiurisrcbin

  # Section 1 — nvmultiurisrcbin direct
  # NOTE: ip_address and port are NOT configured — setting ip-address via
  # g_object_set causes SIGSEGV in DeepStream 8.0. REST API stays disabled.
  max_batch_size: 4 # max sources to mux (NOT batch_size)
  mode: 0 # 0=video-only  1=audio-only

  # Section 2 — per-source nvurisrcbin
  gpu_id: 0
  num_extra_surfaces: 9
  cudadec_memtype: 0 # 0=device(GPU)  1=pinned  2=unified
  dec_skip_frames: 0 # 0=all  1=non-ref  2=key-only
  drop_frame_interval: 0
  select_rtp_protocol: 4 # 0=multi  4=TCP-only (recommended)
  rtsp_reconnect_interval: 10 # seconds; 0=disable
  rtsp_reconnect_attempts: -1 # -1=infinite
  latency: 400 # RTSP jitter buffer ms (default 100)
  udp_buffer_size: 4194304 # bytes; 4MB (default 524288)
  disable_audio: false
  drop_pipeline_eos: true # prevent one-source EOS killing pipeline

  # Section 3 — nvstreammux passthrough
  width: 1920
  height: 1080
  batched_push_timeout: 40000 # µs to wait for full batch
  live_source: true
  sync_inputs: false

  cameras:
    - id: camera-entrance
      uri: rtsp://192.168.1.100:554/stream1
    - id: camera-parking
      uri: rtsp://192.168.1.101:554/stream1

  # Smart Record — flat integer enum properties (NOT a sub-object)
  # smart_rec_video_cache is DEPRECATED — use smart_rec_cache
  smart_record: 2 # 0=disable  1=cloud-only  2=multi(cloud+local)
  smart_rec_dir_path: "/opt/engine/data/rec"
  smart_rec_file_prefix: "lsr"
  smart_rec_cache: 10 # pre-event buffer seconds
  smart_rec_default_duration: 20
  smart_rec_mode: 0 # 0=audio+video  1=video-only  2=audio-only
  smart_rec_container: 0 # 0=mp4  1=mkv
```

```cpp
struct CameraConfig {
    std::string id;   ///< camera identifier, used as element name
    std::string uri;
};

struct SourcesConfig {
    // Section 1 — nvmultiurisrcbin direct
    std::string type          = "nvmultiurisrcbin";
    // NOTE: ip_address and port deliberately absent — setting ip-address via
    // g_object_set causes SIGSEGV in DeepStream 8.0. Omit entirely.
    int  max_batch_size       = 1;        // NOT batch_size
    int  mode                 = 0;        // 0=video 1=audio
    // Section 2 — nvurisrcbin per-source
    int  gpu_id               = 0;
    int  num_extra_surfaces   = 1;
    int  cudadec_memtype      = 0;        // 0=device 1=pinned 2=unified
    int  dec_skip_frames      = 0;        // 0=all 1=non-ref 2=key-only
    int  drop_frame_interval  = 0;
    int  select_rtp_protocol  = 0;        // 0=multi 4=TCP-only
    int  rtsp_reconnect_interval = 10;
    int  rtsp_reconnect_attempts = -1;    // -1=infinite
    int  latency              = 100;      // ms
    int  udp_buffer_size      = 524288;   // bytes
    bool disable_audio        = true;
    bool drop_pipeline_eos    = false;
    // Section 3 — nvstreammux passthrough
    int  width                = 1920;
    int  height               = 1080;
    int  batched_push_timeout = 40000;    // µs
    bool live_source          = true;
    bool sync_inputs          = false;
    // cameras
    std::vector<CameraConfig> cameras;
    // smart record — flat integer enum (NOT a SmartRecordConfig sub-object)
    int  smart_record         = 0;        // 0=disable 1=cloud 2=multi(cloud+local)
    std::string smart_rec_dir_path;
    std::string smart_rec_file_prefix = "lsr";
    int  smart_rec_cache      = 0;        // pre-event buffer sec (replaces deprecated smart_rec_video_cache)
    int  smart_rec_default_duration = 20;
    int  smart_rec_mode       = 0;        // 0=AV 1=video 2=audio
    int  smart_rec_container  = 0;        // 0=mp4 1=mkv

    // NO output_queue — queue is per-element (queue: {} inline on each element)
};
```

#### Processing

`elements` is an ordered list — elements are linked in declaration order.
Each element declares `queue: {}` inline to insert a `GstQueue` before itself.

```yaml
processing:
  elements:
    - id: pgie_detection
      type: nvinfer # nvinfer (TensorRT direct) | nvinferserver (Triton)
      role: primary_inference # primary_inference | secondary_inference
      unique_id: 1 # gie-unique-id — identifies metadata downstream
      config_file: "/opt/engine/data/components/pgie/config.pbtxt"
      process_mode: 1 # 1=Primary (full-frame)  2=Secondary (per-object)
      interval: 3 # skip N batches between inferences (0=every frame)
      batch_size: 4 # must equal sources.max_batch_size
      gpu_id: 0
      queue: {} # → GstQueue before pgie (use queue_defaults)

    # SGIE example (commented — uncomment to enable secondary inference)
    # - id: sgie_classification
    #   type: nvinfer            # nvinfer (TensorRT) | nvinferserver (Triton)
    #   role: secondary_inference
    #   unique_id: 3
    #   config_file: "/opt/engine/data/components/sgie/config.pbtxt"
    #   process_mode: 2          # Secondary: runs on objects, NOT full frames
    #   operate_on_gie_id: 1     # only run on objects from GIE with unique_id=1
    #   operate_on_class_ids: "3" # colon-separated class IDs from parent GIE
    #   batch_size: 16
    #   gpu_id: 0
    #   queue: {}

    - id: tracker
      type: nvtracker
      ll_lib_file: "/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so"
      ll_config_file: "/opt/engine/data/config/tracker_NvDCF_perf.yml"
      tracker_width: 640 # frame scaled to this size before tracking
      tracker_height: 640
      gpu_id: 0
      compute_hw: 1 # 0=default  1=GPU  2=VIC (Jetson only)
      user_meta_pool_size: 512
      queue: {} # → GstQueue before tracker

    - id: demuxer
      type: nvstreamdemux
      queue: {} # → GstQueue before demuxer
```

```cpp
struct ProcessingElementConfig {
    std::string id;
    // "nvinfer" (TensorRT) | "nvinferserver" (Triton) | "nvtracker" | "nvstreamdemux"
    std::string type;
    // "primary_inference" | "secondary_inference" | "tracker" | "demuxer"
    std::string role;
    bool enable = true;

    // nvinfer / nvinferserver fields
    std::optional<int>         unique_id;
    std::optional<std::string> config_file;
    std::optional<int>         interval;
    std::optional<int>         process_mode;     // 1=Primary  2=Secondary (NOT "server"|"direct")
    std::optional<int>         batch_size;
    std::optional<int>         gpu_id;
    // SGIE-only (require process_mode=2)
    std::optional<int>         operate_on_gie_id;
    std::optional<std::string> operate_on_class_ids; // colon-separated class IDs

    // nvtracker fields
    std::optional<std::string> ll_lib_file;      // path to .so tracker library
    std::optional<std::string> ll_config_file;   // path to tracker config YAML
    std::optional<int>         tracker_width;
    std::optional<int>         tracker_height;
    std::optional<int>         compute_hw;       // 0=default 1=GPU 2=VIC
    std::optional<int>         user_meta_pool_size;

    // queue injected before this element
    std::optional<QueueConfig> queue;
};

struct ProcessingConfig {
    std::vector<ProcessingElementConfig> elements;
    // NO output_queue — queue is per-element (queue: {} inline)
};
```

#### Outputs

`elements` list follows GStreamer link order — identical pattern to `processing` and `visuals`.
Each element uses `queue: {}` to insert a `GstQueue` before itself.

Full RTSP output chain (confirmed from `demo_build_graph.dot`):

```
queue → nvvideoconvert → capsfilter → nvv4l2h264enc → queue → h264parse → queue → rtspclientsink
```

```yaml
outputs:
  - id: rtsp_out
    type: rtsp_client
    elements:
      - id: preencode_convert
        type: nvvideoconvert
        nvbuf_memory_type: nvbuf-mem-cuda-device
        src_crop: "0:0:0:0"
        dest_crop: "0:0:0:0"
        queue: {} # → entry queue before videoconvert (thread boundary from visuals)

      - id: preencode_caps
        type: capsfilter
        caps: "video/x-raw(memory:NVMM), format=(string)NV12, width=(int)1920, height=(int)1080"

      - id: encoder
        type: nvv4l2h264enc # nvv4l2h264enc | nvv4l2h265enc
        bitrate: 3000000 # bps
        control_rate: cbr # cbr | vbr
        profile: main # baseline | main | high
        iframeinterval: 30 # keyframe every N frames

      - id: parser
        type: h264parse # h264parse | h265parse
        queue: # → queue after encoder; absorbs I-frame bitrate bursts
          max_size_buffers: 20
          max_size_bytes_mb: 40
          max_size_time_sec: 1.5
          leaky: 2

      - id: sink
        type: rtspclientsink
        location: rtsp://192.168.1.99:8554/de1
        protocols: tcp # tcp | udp
        queue: {} # → queue after parser, before sink
```

```cpp
struct OutputElementConfig {
    std::string id;
    std::string type;   // "nvvideoconvert" | "capsfilter"
                        // | "nvv4l2h264enc" | "nvv4l2h265enc"
                        // | "h264parse" | "h265parse"
                        // | "rtspclientsink" | "filesink"
    // nvvideoconvert
    std::optional<std::string> nvbuf_memory_type;
    std::optional<std::string> src_crop;
    std::optional<std::string> dest_crop;
    // capsfilter
    std::optional<std::string> caps;
    // encoder (nvv4l2h264enc / nvv4l2h265enc)
    std::optional<int>         bitrate;          // bps
    std::optional<std::string> control_rate;     // "cbr" | "vbr"
    std::optional<std::string> profile;
    std::optional<int>         iframeinterval;
    // sink fields
    std::optional<std::string> location;         // rtsp URI or file path
    std::optional<std::string> protocols;        // "tcp" | "udp"
    // queue injected before this element
    std::optional<QueueConfig> queue;
};

struct OutputConfig {
    std::string id;
    std::string type;   // "rtsp_client" | "filesink" | "display"
    bool enable = true;
    std::vector<OutputElementConfig> elements;
};
```

#### Analytics

```yaml
analytics:
  id: "analytics_main"
  enable: true
  input_from: "tracker_main"
  unique_id: 10
  osd_mode: 2
  stream_configs:
    - stream_id: 0
      config_width: 1920
      config_height: 1080
      roi_filtering:
        enable: true
        rois:
          - label: "ParkingZoneA"
            points: [[100, 200], [500, 200], [500, 600], [100, 600]]
        class_ids: [0, 2]
      line_crossing:
        enable: true
        lines:
          - label: "EntryLine"
            x1d: 100
            y1d: 300
            x2d: 200
            y2d: 300
            x1c: 100
            y1c: 300
            x2c: 500
            y2c: 300
            mode: "balanced"
        class_ids: [0]
```

#### Custom Handlers

```yaml
custom_handlers:
  - id: "vehicle_handler"
    enable: true
    type: "on_detect"
    source_from: "pgie_detector"
    signal_from: "osd_main"
    signal_name: "new-sample"
    label_filter: ["car", "truck", "bus"]
    min_confidence: 0.7
    min_interval: 1000 # ms between triggers per object
    snapshot: 2 # 0=none, 1=crop, 2=crop+fullframe
    evidence_from: "tiler"
    mbroker_host: "localhost"
    mbroker_port: "6379"
    mbroker_channel: "vms:vehicles"
    processing_rules:
      license_plate:
        endpoint_url: "http://lpr:8080/recognize"
        result_json_path: "result.plate_text"
        display_prefix: "Plate: "
```

### Config Parser Implementation

```cpp
// infrastructure/config_parser/yaml_config_parser.hpp
namespace engine::infrastructure::config_parser {

class YamlConfigParser : public engine::core::config::IConfigParser {
public:
    ParseResult parse(const std::string& config_file_path) override;

private:
    // Each section has a dedicated parser function (single responsibility)
    void parse_application(const YAML::Node& node, PipelineConfig& config);
    void parse_sources(const YAML::Node& node, PipelineConfig& config);
    void parse_muxer(const YAML::Node& node, PipelineConfig& config);
    void parse_processing(const YAML::Node& node, PipelineConfig& config);
    void parse_analytics(const YAML::Node& node, PipelineConfig& config);
    void parse_visuals(const YAML::Node& node, PipelineConfig& config);
    void parse_outputs(const YAML::Node& node, PipelineConfig& config);
    void parse_recording(const YAML::Node& node, PipelineConfig& config);
    void parse_messaging(const YAML::Node& node, PipelineConfig& config);
    void parse_storage(const YAML::Node& node, PipelineConfig& config);
    void parse_handlers(const YAML::Node& node, PipelineConfig& config);
    void parse_services(const YAML::Node& node, PipelineConfig& config);
    void parse_api(const YAML::Node& node, PipelineConfig& config);
};

} // namespace engine::infrastructure::config_parser
```

---

## Event & Probe System

### Two Metadata Access Mechanisms

| Mechanism | Access Point        | Can Modify? | Latency | Use Case                         |
| --------- | ------------------- | ----------- | ------- | -------------------------------- |
| **Probe** | Any pad in pipeline | Read+Write  | Minimal | Metadata modification, real-time |

### Probe Flow (Probe Handler)

```
Camera → Decoder → Infer →[PROBE]→ Tracker →[PROBE]→ OSD → Sink
                           │                  │
                    IProbeHandler         IProbeHandler
                    ├── Modify class_id   ├── Restore class_id
                    └── Crop objects      └── Trigger recording
```

### class_id Namespacing (Around Tracker)

When running multiple detectors with overlapping `class_id` values, two probes guard the tracker:

```
Before tracker (sink pad):  offset class_id by unique_component_id
                      ↓
               [nvtracker]
                      ↓
After tracker (src pad):    restore class_id to original value
```

This prevents label flickering in OSD when multiple inference engines produce overlapping class IDs.

---

## Infrastructure Adapters

### Messaging

| Adapter               | Protocol      | Interface          | Use Case                   |
| --------------------- | ------------- | ------------------ | -------------------------- |
| `RedisStreamProducer` | Redis Streams | `IMessageProducer` | Real-time event publishing |
| `KafkaAdapter`        | Apache Kafka  | `IMessageProducer` | High-throughput event log  |

```yaml
broker_configurations:
  - id: "redis_events"
    type: "redis"
    host: "redis"
    port: 6379
    channel: "vms:events"

  - id: "kafka_logs"
    type: "kafka"
    broker_list: "kafka:9092"
    topic: "vms.events"
```

### Storage

| Adapter               | Backend    | Interface         | Use Case              |
| --------------------- | ---------- | ----------------- | --------------------- |
| `LocalStorageManager` | Local FS   | `IStorageManager` | Dev, edge deployment  |
| `S3StorageManager`    | S3 / MinIO | `IStorageManager` | Cloud, shared storage |

```yaml
storage_configurations:
  - id: "local_recordings"
    type: "local"
    base_path: "/data/recordings"
    max_size_gb: 100

  - id: "s3_snapshots"
    type: "s3"
    endpoint: "http://minio:9000"
    bucket: "vms-snapshots"
    access_key: "${MINIO_ACCESS_KEY}"
    secret_key: "${MINIO_SECRET_KEY}"
```

### REST API (Runtime Control)

```
POST /api/v1/pipeline/start        → PipelineManager::start()
POST /api/v1/pipeline/stop         → PipelineManager::stop()
POST /api/v1/pipeline/pause        → PipelineManager::pause()
GET  /api/v1/pipeline/status       → PipelineManager::get_info()
POST /api/v1/streams/add           → RuntimeStreamManager::add_stream()
POST /api/v1/streams/remove        → RuntimeStreamManager::remove_stream()
GET  /api/v1/health                → Health check
```

---

## Pipeline Lifecycle

### State Machine

```
                               initialize()
    ┌──────────┐              ┌───────────┐              ┌──────────┐
    │UNINIT    │─────────────►│  READY    │─────────────►│ PLAYING  │
    └──────────┘              └───────────┘   start()    └──────────┘
         ▲                         ▲                          │
         │                         │         pause()          │
         │         stop()          │    ┌──────────┐          │
         │◄────────────────────────│◄───│  PAUSED  │◄─────────┘
         │                              └──────────┘
         │
    Error at any state ──────────► ERROR ──────► stop() possible
```

### Lifecycle Phases

```cpp
// 1. Construction & Config Parsing
auto config_parser = std::make_shared<YamlConfigParser>();
auto result = config_parser->parse(config_file_path);
auto config = std::get<PipelineConfig>(result);

// 2. GStreamer Initialization
gst_init(&argc, &argv);
GMainLoop* main_loop = g_main_loop_new(nullptr, FALSE);

// 3. Pipeline Manager Creation
auto factory = std::make_shared<BuilderFactory>();
auto manager = std::make_unique<PipelineManager>(builder, handler_manager);

// 4. Initialization (builds pipeline graph)
manager->initialize(config, main_loop);

// 5. Start (PLAYING state)
manager->start();

// 7. Main Loop (blocks until signal)
g_main_loop_run(main_loop);

// 8. Cleanup
manager->stop();
g_main_loop_unref(main_loop);
gst_deinit();
```

### GstBus Message Handling

The `PipelineManager` installs a bus watch to process asynchronous GStreamer messages:

| Message Type                | Action                                          |
| --------------------------- | ----------------------------------------------- |
| `GST_MESSAGE_EOS`           | Log, emit event, optionally restart pipeline    |
| `GST_MESSAGE_ERROR`         | Log error, transition to ERROR state, quit loop |
| `GST_MESSAGE_WARNING`       | Log warning                                     |
| `GST_MESSAGE_STATE_CHANGED` | Log state transitions, export DOT file          |
| `GST_MESSAGE_ELEMENT`       | Ignore or forward to probe handlers if needed   |

---

## Output & Recording System

### Output Types

```
┌─────────────────────────────────────────────────────────────────┐
│                       Output Architecture                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  [Processing] ──► [Demuxer] ──► [Per-Stream Outputs]            │
│                       │                                          │
│                       ├──► Stream 0 ──► [Tiler] ──► [OSD] ──► Sink │
│                       ├──► Stream 1 ──► Sink                    │
│                       └──► Stream N ──► ...                      │
│                                                                  │
│  Output Types:                                                   │
│    ├── Display (nveglglessink / nvdrmvideosink)                 │
│    ├── File (nvv4l2h264enc → mux → filesink)                    │
│    ├── RTSP (nvv4l2h264enc → rtspclientsink)                    │
│    └── Fake (fakesink for headless processing)                  │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### Smart Recording

```yaml
smart_record:
  id: "smart_rec"
  enable: true
  input_from: "demuxer_main"
  container: "mp4"
  storage_target_id: "local_recordings"
  default_duration: 10 # seconds
  cache_size: 15 # seconds of lookback buffer
  trigger_on:
    labels: ["car", "person"]
    min_confidence: 0.7
    analytics_events: ["line_crossing", "roi_entry"]
```

**Smart Record Flow:**

```
Probe detects event ─► NvDsSRStart() on source bin
                            │
                            ▼
                    GstSmartRecordBin (nv-specific)
                    ├── Lookback buffer (cached frames)
                    ├── Start recording from buffer
                    ├── Continue for configured duration
                    └── Stop → Save to storage target
```

---

## Analytics System

### nvdsanalytics Capabilities

| Feature                     | Description                                          |
| --------------------------- | ---------------------------------------------------- |
| **ROI Filtering**           | Filter objects to specific polygonal regions         |
| **Line Crossing Detection** | Detect objects crossing defined lines with direction |
| **Overcrowding Detection**  | Alert when object count exceeds threshold in region  |
| **Direction Detection**     | Determine movement direction of tracked objects      |

### Analytics Config Flow

1. YAML config defines analytics rules per stream
2. At build time, `AnalyticsBuilder` generates `nvdsanalytics_config.txt` INI file
3. `nvdsanalytics` element reads generated config
4. Metadata attached to `NvDsFrameMeta` / `NvDsObjectMeta`
5. Event handlers read analytics metadata for event publishing

```
YAML Config ──► AnalyticsBuilder ──► Generated INI File ──► nvdsanalytics ──► Metadata
```

---

## Design Patterns

| Pattern                     | Where                                     | Purpose                                           |
| --------------------------- | ----------------------------------------- | ------------------------------------------------- |
| **Builder**                 | `block_builders/`, `builders/`            | Sequential pipeline construction from config      |
| **Abstract Factory**        | `IBuilderFactory` / `BuilderFactory`      | Create typed element builders (no config slice)   |
| **Full Config Pattern**     | All `IElementBuilder::build()` impls      | Each builder reads what it needs from full config |
| **Strategy**                | `IProbeHandler`                           | Interchangeable probe processing                  |
| **Observer**                | GstBus watch, `pad-added` signals         | Async event notification                          |
| **Chain of Responsibility** | `ProcessingBuilder` flow items            | Sequential processing stages                      |
| **Template Method**         | `BaseBuilder.build()`                     | Common build flow with customizable steps         |
| **Facade**                  | `PipelineManager`                         | Single entry point for pipeline lifecycle         |
| **Adapter**                 | `RedisStreamProducer`, `S3StorageManager` | External system integration                       |
| **Singleton**               | Logger (`spdlog`)                         | Global logging instance                           |
| **Plugin**                  | `plugins/` + `HandlerRegistry`            | Runtime-loadable handlers (.so)                   |
| **Composite**               | `GstBin` containing `GstElement`s         | Treat groups of elements as single unit           |

---

## Memory Management

> **Full RAII guide → [`docs/architecture/RAII.md`](RAII.md)**  
> Covers: heap memory, file handles, sockets, mutex/locks, timers, scope guards,
> GStreamer resources, NvDs metadata rules, custom destructor classes, anti-patterns.

### GStreamer / GLib Ownership Rules

| Object                           | Obtained via                          | Release via                                                | Notes                             |
| -------------------------------- | ------------------------------------- | ---------------------------------------------------------- | --------------------------------- |
| `GstElement*` (floating/unowned) | `gst_element_factory_make()`          | `gst_object_unref()`                                       | Caller owns until `gst_bin_add()` |
| `GstElement*` in bin             | `gst_bin_add(bin, elem)`              | _(bin owns)_                                               | **Do NOT unref after add**        |
| `GstPad*`                        | `gst_element_get_static_pad()`        | `gst_object_unref()`                                       | Must unref even read-only use     |
| `GstPad*` (request)              | `gst_element_get_request_pad()`       | `gst_element_release_request_pad()` + `gst_object_unref()` | Release before unref              |
| `GstCaps*`                       | `gst_caps_new_*()`, `gst_caps_copy()` | `gst_caps_unref()`                                         | Reference-counted                 |
| `GstBus*`                        | `gst_pipeline_get_bus()`              | `gst_object_unref()`                                       |                                   |
| `GMainLoop*`                     | `g_main_loop_new()`                   | `g_main_loop_unref()`                                      |                                   |
| `GError*`                        | set by GStreamer (out param)          | `g_error_free()`                                           | Check non-null before freeing     |
| `gchar*`                         | `g_object_get()`, `g_strdup()`        | `g_free()`                                                 | GLib heap allocation              |
| `NvDsBatchMeta*`                 | `gst_buffer_get_nvds_batch_meta()`    | **DO NOT FREE**                                            | Owned by GstBuffer / pipeline     |
| `NvDsFrameMeta*`                 | iterated from `batch_meta`            | **DO NOT FREE**                                            |                                   |
| `NvDsObjectMeta*`                | iterated from `frame_meta`            | **DO NOT FREE**                                            |                                   |

### RAII Strategy — `gst_utils.hpp`

`core/include/engine/core/utils/gst_utils.hpp` (header-only — no `.cpp` needed):

```cpp
namespace engine::core::utils {

// Elements — ONLY before gst_bin_add(); call release() after successful add.
using GstElementPtr = std::unique_ptr<GstElement, decltype(&gst_object_unref)>;
inline GstElementPtr make_gst_element(const char* factory, const char* name) {
    return GstElementPtr(gst_element_factory_make(factory, name), gst_object_unref);
}

using GstCapsPtr   = std::unique_ptr<GstCaps,   decltype(&gst_caps_unref)>;
using GstPadPtr    = std::unique_ptr<GstPad,    decltype(&gst_object_unref)>;
using GstBusPtr    = std::unique_ptr<GstBus,    decltype(&gst_object_unref)>;
using GMainLoopPtr = std::unique_ptr<GMainLoop, decltype(&g_main_loop_unref)>;
using GErrorPtr    = std::unique_ptr<GError,    decltype(&g_error_free)>;
using GCharPtr     = std::unique_ptr<gchar,     decltype(&g_free)>;

} // namespace engine::core::utils
```

**Builder error-path pattern** — guard disarms on success, auto-cleans all failure paths:

```cpp
GstElement* SourceBuilder::build(const PipelineConfig& config, int index) {
    auto src = engine::core::utils::make_gst_element("nvmultiurisrcbin", "src");
    if (!src) { LOG_E("Failed to create nvmultiurisrcbin"); return nullptr; }

    g_object_set(G_OBJECT(src.get()), "max-batch-size", (guint)config.sources.max_batch_size, nullptr);

    if (!gst_bin_add(GST_BIN(pipeline_), src.get())) {
        LOG_E("Failed to add src to pipeline");
        return nullptr;  // ~GstElementPtr → gst_object_unref() automatically
    }
    return src.release();  // pipeline owns — disarm guard
}
```

For detailed patterns (heap, sockets, mutex/locks, timers, scope guards, custom destructor classes) → **[RAII.md](RAII.md)**.

---

## Namespace Convention

### Root Namespace: `engine`

| Namespace                               | Maps To                               |
| --------------------------------------- | ------------------------------------- |
| `engine::core::pipeline`                | `core/include/engine/core/pipeline/`  |
| `engine::core::builders`                | `core/include/engine/core/builders/`  |
| `engine::core::config`                  | `core/include/engine/core/config/`    |
| `engine::core::events`                  | `core/include/engine/core/eventing/`  |
| `engine::core::probes`                  | `core/include/engine/core/probes/`    |
| `engine::core::handlers`                | `core/include/engine/core/handlers/`  |
| `engine::core::messaging`               | `core/include/engine/core/messaging/` |
| `engine::core::storage`                 | `core/include/engine/core/storage/`   |

| `engine::core::runtime`                 | `core/include/engine/core/runtime/`   |
| `engine::core::utils`                   | `core/include/engine/core/utils/`     |
| `engine::pipeline`                      | `pipeline/include/engine/pipeline/`   |
| `engine::pipeline::block_builders`      | `pipeline/.../block_builders/`        |
| `engine::pipeline::builders`            | `pipeline/.../builders/`              |
| `engine::pipeline::linking`             | `pipeline/.../linking/`               |
| `engine::pipeline::probes`              | `pipeline/.../probes/`                |
| `engine::domain`                        | `domain/include/engine/domain/`       |
| `engine::infrastructure::config_parser` | `infrastructure/config_parser/`       |
| `engine::infrastructure::messaging`     | `infrastructure/messaging/`           |
| `engine::infrastructure::storage`       | `infrastructure/storage/`             |
| `engine::infrastructure::rest_api`      | `infrastructure/rest_api/`            |

### Naming Conventions

| Element     | Convention                                 | Example                             |
| ----------- | ------------------------------------------ | ----------------------------------- |
| Namespaces  | `snake_case`                               | `engine::core::pipeline`            |
| Classes     | `PascalCase`                               | `PipelineManager`, `BuilderFactory` |
| Interfaces  | `IPascalCase`                              | `IPipelineManager`, `IProbeHandler` |
| Methods     | `snake_case`                               | `build_pipeline()`, `get_state()`   |
| Member vars | `snake_case_`                              | `pipeline_`, `config_`, `tails_`    |
| Constants   | `UPPER_SNAKE_CASE`                         | `DEFAULT_MBROKER_PORT`              |
| Enums       | `PascalCase` (type), `PascalCase` (values) | `PipelineState::Playing`            |
| Files       | `snake_case.hpp` / `.cpp`                  | `pipeline_manager.hpp`              |
| Config IDs  | `snake_case`                               | `"pgie_detector"`, `"muxer_main"`   |

---

## Build System

> 📖 **Full CMake Reference** → [`docs/architecture/CMAKE.md`](CMAKE.md)  
> Bao gồm: Configure commands, build types, dependency management, FetchContent, presets, custom targets, anti-patterns.

### CMake Structure

```
CMakeLists.txt (root)
├── app/CMakeLists.txt             # vms_engine executable
├── core/CMakeLists.txt            # vms_engine_core static library
├── pipeline/CMakeLists.txt        # vms_engine_pipeline static library
├── domain/CMakeLists.txt          # vms_engine_domain static library
├── infrastructure/CMakeLists.txt  # vms_engine_infrastructure static library
└── plugins/CMakeLists.txt         # Plugin shared libraries
```

### Library Dependencies

```
vms_engine (executable)
  ├── vms_engine_core              # Interfaces, config types, utils
  ├── vms_engine_pipeline          # DeepStream builders, manager
  │   └── depends on: vms_engine_core
  ├── vms_engine_domain            # Business rules
  │   └── depends on: vms_engine_core
  └── vms_engine_infrastructure    # YAML parser, Redis, S3
      └── depends on: vms_engine_core
```

### External Dependencies

| Dependency      | Discovery Method     | Required? | Notes                |
| --------------- | -------------------- | --------- | -------------------- |
| GLib 2.56+      | `pkg_check_modules`  | Yes       | GStreamer foundation |
| GStreamer 1.14+ | `pkg_check_modules`  | Yes       | Multimedia framework |
| DeepStream SDK  | `DEEPSTREAM_DIR` env | Yes       | AI video analytics   |
| spdlog 1.12+    | `FetchContent`       | Yes       | Logging              |
| yaml-cpp        | `FetchContent`       | Yes       | Config parsing       |
| libcurl         | `pkg_check_modules`  | Yes       | HTTP client          |
| Threads         | `find_package`       | Yes       | std::thread support  |
| hiredis         | `pkg_check_modules`  | Optional  | Redis client         |
| librdkafka      | `pkg_check_modules`  | Optional  | Kafka client         |
| Pistache        | `find_package`       | Optional  | REST API server      |

### Build Commands

```bash
# Configure (Debug)
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream

# Build
cmake --build build -- -j$(nproc)

# Run
./build/bin/vms_engine -c configs/default.yml

# Release build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -- -j$(nproc)
```

### Build Output

```
build/
├── bin/
│   ├── vms_engine                  # Main executable
│   └── config/                     # Copied YAML configs
├── lib/
│   ├── libvms_engine_core.a
│   ├── libvms_engine_pipeline.a
│   ├── libvms_engine_domain.a
│   └── libvms_engine_infrastructure.a
└── compile_commands.json           # For clangd/IDE
```

---

## Migration from lantanav2

### Key Changes

| Aspect                | lantanav2                                             | vms-engine                                                                       |
| --------------------- | ----------------------------------------------------- | -------------------------------------------------------------------------------- |
| **Project name**      | `lantana`                                             | `vms_engine`                                                                     |
| **Root namespace**    | `lantana::`                                           | `engine::`                                                                       |
| **Include prefix**    | `lantana/core/`, `lantana/backends/deepstream/`       | `engine/core/`, `engine/pipeline/`                                               |
| **Backend structure** | `backends/deepstream/` + `backends/dlstreamer/`       | `pipeline/` (single, flat)                                                       |
| **Backend config**    | `std::variant<DeepStream, DLStreamer>`                | Direct DeepStream types                                                          |
| **Builder factory**   | `DsBuilderFactory` (passes config slices)             | `BuilderFactory` (no config at creation)                                         |
| **Builder signature** | `build(const SpecificConfig& slice, int idx)`         | `build(const PipelineConfig& cfg, int idx)`                                      |
| **Pipeline manager**  | `DsPipelineManager`                                   | `PipelineManager`                                                                |
| **Element builders**  | `ds_source_builder.hpp`                               | `source_builder.hpp`                                                             |
| **Executable**        | `lantana`                                             | `vms_engine`                                                                     |
| **Library names**     | `liblantana_*.so`                                     | `libvms_engine_*.a`                                                              |
| **Config prefix**     | `deepstream_*.yml`                                    | `default.yml`, `example_*.yml`                                                   |
| **YAML sources**      | `sources[].backend_options.deepstream.*`              | `sources.cameras[]` + flat fields                                                |
| **YAML processing**   | `processing_flow[].backend_config.deepstream.*`       | `processing.elements[]` flat fields                                              |
| **YAML queue**        | Implicit / `QueueManager` internal heuristics         | Explicit `queue: {}` inline per element                                          |
| **YAML smart_record** | `sources[].backend_options.deepstream.smart_record.*` | `sources.smart_record` (int enum) + `sources.smart_rec_*` flat fields            |
| **YAML outputs**      | `outputs[].encoding.*` + `outputs[].destination.*`    | `outputs[].elements[]` flat list (nvvideoconvert→capsfilter→encoder→parser→sink) |

### File Mapping (Key Files)

| lantanav2 Path                                                | vms-engine Path                                                |
| ------------------------------------------------------------- | -------------------------------------------------------------- |
| `core/include/lantana/core/pipeline/ipipeline_manager.hpp`    | `core/include/engine/core/pipeline/ipipeline_manager.hpp`      |
| `core/include/lantana/core/builders/ibuilder_factory.hpp`     | `core/include/engine/core/builders/ibuilder_factory.hpp`       |
| `backends/deepstream/include/.../ds_pipeline_manager.hpp`     | `pipeline/include/engine/pipeline/pipeline_manager.hpp`        |
| `backends/deepstream/include/.../ds_builder_factory.hpp`      | `pipeline/include/engine/pipeline/builder_factory.hpp`         |
| `backends/deepstream/include/.../block_builders/*.hpp`        | `pipeline/include/engine/pipeline/block_builders/*.hpp`        |
| `backends/deepstream/include/.../builders/ds_*.hpp`           | `pipeline/include/engine/pipeline/builders/*.hpp`              |
| `backends/deepstream/include/.../probes/*.hpp`                | `pipeline/include/engine/pipeline/probes/*.hpp`                |
| `infrastructure/config_parser/...`                            | `infrastructure/config_parser/...`                             |
| `infrastructure/messaging/...`                                | `infrastructure/messaging/...`                                 |
| `infrastructure/storage/...`                                  | `infrastructure/storage/...`                                   |
| `domain/include/lantana/domain/...`                           | `domain/include/engine/domain/...`                             |

### Migration Checklist

- [ ] Create new directory structure per this blueprint
- [ ] Copy and rename all files (drop `ds_` prefix, `lantana` → `engine`)
- [ ] Update all `#include` paths (`lantana/` → `engine/`)
- [ ] Update all namespace declarations (`lantana::` → `engine::`)
- [ ] Remove DLStreamer-related code and `std::variant` wrappers
- [ ] Simplify config types (remove `ProcessingBackendOptions` variant)
- [ ] Flatten `backends/deepstream/` into `pipeline/`
- [ ] Update CMakeLists.txt for new directory structure
- [ ] Update build targets and library names
- [ ] Update Dockerfiles
- [ ] Update config file references
- [ ] Test build and runtime

---

## Quick Reference

### Common Operations

| Task                      | Command / Action                                             |
| ------------------------- | ------------------------------------------------------------ |
| Build (debug)             | `cmake --build build -- -j$(nproc)`                          |
| Run                       | `./build/bin/vms_engine -c configs/default.yml`              |
| Export DOT graph          | Set `dot_file_dir` in YAML; auto-exported                    |
| View DOT graph            | `dot -Tpng graph.dot -o graph.png`                           |
| Add new element builder   | Create in `pipeline/builders/`, register in `BuilderFactory` |
| Add new probe             | Implement `IProbeHandler`, register in `ProbeHandlerManager` |
| Add new messaging adapter | Implement `IMessageProducer` in `infrastructure/messaging/`  |
| Add new storage backend   | Implement `IStorageManager` in `infrastructure/storage/`     |

### Adding a New Pipeline Element Builder

1. Create header: `pipeline/include/engine/pipeline/builders/my_builder.hpp`
2. Create source: `pipeline/src/builders/my_builder.cpp`
3. Implement `IElementBuilder::build(const engine::core::config::PipelineConfig& config, int index = 0)`
   — access your section via `config.processing.elements[index]` or whatever is relevant
4. Add factory method to `IBuilderFactory` (if new type) — no config arg at creation
5. Register in `BuilderFactory` (map role/type string → builder class)
6. Add config struct to `core/include/engine/core/config/config_types.hpp`
7. Add YAML parser section in `infrastructure/config_parser/`

### Adding a New Probe Handler

1. Create header: `pipeline/include/engine/pipeline/probes/my_probe_handler.hpp`
2. Create source: `pipeline/src/probes/my_probe_handler.cpp`
3. Implement `configure(const EventHandlerConfig&)` + `static GstPadProbeReturn on_buffer(…)`
4. Register trigger string in `ProbeHandlerManager::attach_probes()` dispatch block
5. Add YAML entry under `event_handlers:` with appropriate `trigger:` value

### Logging Macros

```cpp
#include "engine/core/utils/logger.hpp"

LOG_T("Trace: detailed debug info");
LOG_D("Debug: {}", variable);
LOG_I("Info: Building source: {} (type: {})", id, type);
LOG_W("Warning: deprecated config field used");
LOG_E("Error: Failed to create element: {}", name);
LOG_C("Critical: Pipeline initialization failed");
```

---

## 📚 Detailed Documentation

For deep-dive technical documentation on the DeepStream-specific implementation, see:

| File                                                                    | Topic                                                              |
| ----------------------------------------------------------------------- | ------------------------------------------------------------------ |
| [`docs/architecture/deepstream/README.md`](deepstream/README.md)        | Index & reading order                                              |
| [`00_project_overview.md`](deepstream/00_project_overview.md)           | Tech stack, pipeline diagram, conventions                          |
| [`01_directory_structure.md`](deepstream/01_directory_structure.md)     | Full project layout with file purposes                             |
| [`02_core_interfaces.md`](deepstream/02_core_interfaces.md)             | All core interfaces (engine:: namespace)                           |
| [`03_pipeline_building.md`](deepstream/03_pipeline_building.md)         | 5-phase build, tails\_ map pattern                                 |
| [`04_linking_system.md`](deepstream/04_linking_system.md)               | Static/dynamic pad linking, queue: {}                              |
| [`05_configuration.md`](deepstream/05_configuration.md)                 | Full YAML schema, parser architecture                              |
| [`06_runtime_lifecycle.md`](deepstream/06_runtime_lifecycle.md)         | GstBus, state machine, RTSP reconnect                              |
| [`07_event_handlers_probes.md`](deepstream/07_event_handlers_probes.md) | GStreamer pad probes, ProbeHandlerManager, SmartRecord/CropObjects |
| [`08_analytics.md`](deepstream/08_analytics.md)                         | nvdsanalytics ROI/line crossing/overcrowding                       |
| [`09_outputs_smart_record.md`](deepstream/09_outputs_smart_record.md)   | Sinks, encoders, smart record API                                  |

---

**Last Updated**: March 2026
**Based On**: lantanav2 architecture + DDD/Clean Architecture principles from family-lineage-app
**Target**: vms-engine (NVIDIA DeepStream 7.1 + C++17)
