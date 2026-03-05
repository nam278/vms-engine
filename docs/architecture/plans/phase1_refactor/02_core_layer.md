---
goal: "Plan 02 — Core Layer: Interfaces, Config Types, RAII Utilities"
version: "1.0"
date_created: "2025-01-15"
last_updated: "2025-07-17"
owner: "VMS Engine Team"
status: "Planned"
tags: [core, interfaces, config-types, raii, logger, c++17, deepstream]
---

# Plan 02 — Core Layer (Interfaces, Config Types, Utilities)

![Status: Planned](https://img.shields.io/badge/status-Planned-blue)

Build the `core/` layer from scratch for vms-engine.
This is the foundation — **zero implementations**, only interfaces + config types + utils.
All config types match the canonical YAML: [`docs/configs/deepstream_default.yml`](../../configs/deepstream_default.yml).

---

## 1. Requirements & Constraints

- **REQ-001**: Define all `I*` interfaces under `core/include/engine/core/` before any implementation in other layers.
- **REQ-002**: Define all config structs in a single `config_types.hpp` matching the canonical YAML schema.
- **REQ-003**: Provide RAII helpers for GStreamer objects in `gst_utils.hpp` (header-only).
- **REQ-004**: Provide logger macros in `logger.hpp` wrapping spdlog (`LOG_T` through `LOG_C`).
- **REQ-005**: `vms_engine_core` library must compile independently with zero implementation code.
- **REQ-006**: All headers use `engine::core::*` namespace — no `lantana::` references.
- **CON-001**: Core must NOT include pipeline, domain, or infrastructure headers (dependency rule: core → ∅).
- **CON-002**: No DeepStream SDK-specific code in core — only GStreamer forward-declares and `<gst/gst.h>`.
- **CON-003**: No `std::variant` for backend selection — vms-engine is DeepStream-native.
- **GUD-001**: Use `has_queue` boolean + `QueueConfig` struct instead of `std::optional<QueueConfig>` for simpler builder logic.
- **GUD-002**: Single `config_types.hpp` over separate per-element config headers to avoid circular dependencies.
- **PAT-001**: Interface-first pattern — define in core, implement in pipeline/domain/infrastructure layers.

---

## 2. Implementation Steps

### Phase 1 — Config Types (Single Header)

**GOAL-001**: Define all config structs in `core/include/engine/core/config/config_types.hpp`.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-001 | Create `config_types.hpp` with all structs matching canonical YAML | ☐ | |
| TASK-002 | Define `QueueConfig` (shared — queue_defaults + inline queue overrides) | ☐ | |
| TASK-003 | Define `PipelineMetaConfig` (id, name, log_level, gst_log_level, dot_file_dir, log_file) | ☐ | |
| TASK-004 | Define `SourcesConfig` with `CameraConfig`, smart record flat properties, 3 property groups | ☐ | |
| TASK-005 | Define `ProcessingElementConfig` + `ProcessingConfig` (nvinfer, nvtracker, nvdsanalytics) | ☐ | |
| TASK-006 | Define `VisualsElementConfig` + `VisualsConfig` (tiler, osd) | ☐ | |
| TASK-007 | Define `OutputElementConfig` + `OutputConfig` (encoders, sinks, msgconv, msgbroker) | ☐ | |
| TASK-008 | Define `EventHandlerConfig` with `BrokerConfig` + `CleanupConfig` (pad probe callbacks) | ☐ | |
| TASK-009 | Define root `PipelineConfig` aggregating all sub-configs | ☐ | |

**File:** `core/include/engine/core/config/config_types.hpp`

```cpp
#pragma once
#include <string>
#include <vector>
#include <optional>

namespace engine::core::config {

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Queue Config (shared — queue_defaults + inline queue: {} overrides)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

struct QueueConfig {
    int max_size_buffers       = 10;
    int max_size_bytes_mb      = 20;
    double max_size_time_sec   = 0.5;
    int leaky                  = 2;    ///< 0=none, 1=upstream, 2=downstream
    bool silent                = true;
};

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Pipeline Metadata
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

struct PipelineMetaConfig {
    std::string id;
    std::string name;
    std::string log_level      = "INFO";     ///< DEBUG | INFO | WARN | ERROR
    std::string gst_log_level  = "*:1";
    std::string dot_file_dir;                ///< empty = disabled
    std::string log_file;                    ///< empty = stdout only
};

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Sources Block (nvmultiurisrcbin)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

struct CameraConfig {
    std::string id;   ///< camera identifier, used as element name
    std::string uri;
};

struct SourcesConfig {
    std::string type = "nvmultiurisrcbin";

    // Group 1 — nvmultiurisrcbin direct
    // NOTE: ip_address and port are intentionally absent.
    // Setting ip-address via g_object_set causes SIGSEGV in DeepStream 8.0.
    // REST API stays disabled; element defaults to 0.0.0.0 internally.
    int max_batch_size         = 4;
    int mode                   = 0;          ///< 0=video, 1=audio

    // Group 2 — nvurisrcbin per-source passthrough
    int gpu_id                 = 0;
    int num_extra_surfaces     = 9;
    int cudadec_memtype        = 0;          ///< 0=device, 1=pinned, 2=unified
    int dec_skip_frames        = 0;          ///< 0=all, 1=non-ref, 2=key-only
    int drop_frame_interval    = 0;
    int select_rtp_protocol    = 4;          ///< 0=multi, 4=TCP-only
    int rtsp_reconnect_interval = 10;
    int rtsp_reconnect_attempts = -1;
    int latency                = 400;
    int udp_buffer_size        = 4194304;
    bool disable_audio         = false;
    bool disable_passthrough   = false;
    bool drop_pipeline_eos     = true;

    // Group 3 — nvstreammux passthrough
    int width                  = 1920;
    int height                 = 1080;
    int batched_push_timeout   = 40000;      ///< µs
    bool live_source           = true;
    bool sync_inputs           = false;

    // Cameras
    std::vector<CameraConfig> cameras;

    // Smart Record — flat properties on nvmultiurisrcbin
    int smart_record           = 0;          ///< 0=disable, 1=cloud-only, 2=multi
    std::string smart_rec_dir_path;
    std::string smart_rec_file_prefix = "lsr";
    int smart_rec_cache        = 10;         ///< pre-event buffer (sec)
    int smart_rec_default_duration = 20;
    int smart_rec_mode         = 0;          ///< 0=audio+video, 1=video, 2=audio
    int smart_rec_container    = 0;          ///< 0=mp4, 1=mkv
};

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Processing Block (nvinfer, nvtracker, nvdsanalytics, ...)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

struct ProcessingElementConfig {
    std::string id;
    std::string type;                        ///< "nvinfer" | "nvtracker" | "nvdsanalytics" | ...
    std::string role;                        ///< "primary_inference" | "tracker" | "analytics" | ...

    // nvinfer properties
    int unique_id              = 0;
    std::string config_file;
    int process_mode           = 1;          ///< 1=primary, 2=secondary
    int interval               = 0;
    int batch_size             = 4;
    int gpu_id                 = 0;
    int operate_on_gie_id      = -1;
    std::string operate_on_class_ids;        ///< "0:2" format

    // nvtracker properties
    std::string ll_lib_file;
    std::string ll_config_file;
    int tracker_width          = 640;
    int tracker_height         = 640;
    int compute_hw             = 0;          ///< 0=default, 1=GPU, 2=VIC
    bool display_tracking_id   = true;
    int user_meta_pool_size    = 512;

    // Inline queue
    bool has_queue             = false;      ///< true if queue: {} present in YAML
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
    std::string type;                        ///< "nvmultistreamtiler" | "nvdsosd"
    int gpu_id                 = 0;

    // nvmultistreamtiler
    int rows                   = 1;
    int columns                = 1;
    int width                  = 1920;
    int height                 = 1080;

    // nvdsosd
    int process_mode           = 1;          ///< 0=cpu, 1=gpu, 2=auto
    bool display_bbox          = true;
    bool display_text          = false;
    bool display_mask          = false;
    int border_width           = 2;

    // Inline queue
    bool has_queue             = false;
    QueueConfig queue;
};

struct VisualsConfig {
    bool enable                = true;
    std::vector<VisualsElementConfig> elements;
};

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Outputs Block (arrays of flat element chains)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

struct OutputElementConfig {
    std::string id;
    std::string type;                        ///< GStreamer factory name

    std::string caps;                        ///< for capsfilter
    std::string nvbuf_memory_type;           ///< for nvvideoconvert
    int bitrate                = 0;          ///< for encoder (bps)
    std::string control_rate;                ///< for encoder
    std::string profile;                     ///< for encoder
    int iframeinterval         = 0;          ///< for encoder
    std::string location;                    ///< for sink (RTSP URL, file path)
    std::string protocols;                   ///< for rtspclientsink

    // Inline queue
    bool has_queue             = false;
    QueueConfig queue;
};

struct OutputConfig {
    std::string id;
    std::string type;                        ///< "rtsp_client" | "file" | "display" | "fake"
    std::vector<OutputElementConfig> elements;
};

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Event Handlers (pad probe callbacks)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

struct BrokerConfig {
    std::string host;
    int port                   = 6379;
    std::string channel;
};

struct CleanupConfig {
    int stale_object_timeout_min = 5;
    int check_interval_batches   = 30;
    int old_dirs_max_days        = 7;
};

struct EventHandlerConfig {
    std::string id;
    bool enable                = true;
    std::string type;                        ///< "on_detect" | "on_eos" | ...
    std::string probe_element;               ///< element id to attach probe
    std::string source_element;              ///< for smart_record: "sources"
    std::string trigger;                     ///< "smart_record" | "crop_object" | ...
    std::vector<std::string> label_filter;

    // Smart record specific
    int pre_event_sec          = 2;
    int post_event_sec         = 20;
    int min_interval_sec       = 2;

    // Crop objects specific
    std::string save_dir;
    int capture_interval_sec   = 5;
    int image_quality          = 85;
    bool save_full_frame       = true;
    std::optional<CleanupConfig> cleanup;

    // Broker (shared)
    std::optional<BrokerConfig> broker;
};

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Root Config — the single top-level struct
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

struct PipelineConfig {
    std::string version;

    PipelineMetaConfig        pipeline;
    QueueConfig               queue_defaults;
    SourcesConfig             sources;
    ProcessingConfig          processing;
    VisualsConfig             visuals;
    std::vector<OutputConfig> outputs;
    std::vector<EventHandlerConfig> event_handlers;
};

} // namespace engine::core::config
```

### Phase 2 — Config Interfaces

**GOAL-002**: Define parser and validator interfaces for config loading/validation.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-010 | Create `IConfigParser` interface (`core/include/engine/core/config/iconfig_parser.hpp`) | ☐ | |
| TASK-011 | Create `IConfigValidator` interface with `ValidationResult` struct | ☐ | |

**File:** `core/include/engine/core/config/iconfig_parser.hpp`

```cpp
#pragma once
#include "engine/core/config/config_types.hpp"
#include <string>

namespace engine::core::config {

/**
 * @brief Parses a YAML file into a PipelineConfig struct.
 */
class IConfigParser {
public:
    virtual ~IConfigParser() = default;
    virtual bool parse(const std::string& file_path, PipelineConfig& config) = 0;
};

} // namespace engine::core::config
```

**File:** `core/include/engine/core/config/iconfig_validator.hpp`

```cpp
#pragma once
#include "engine/core/config/config_types.hpp"
#include <string>
#include <vector>

namespace engine::core::config {

struct ValidationResult {
    bool success = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

/**
 * @brief Validates a PipelineConfig after parsing.
 */
class IConfigValidator {
public:
    virtual ~IConfigValidator() = default;
    virtual ValidationResult validate(const PipelineConfig& config) = 0;
};

} // namespace engine::core::config
```

### Phase 3 — Builder Interfaces

**GOAL-003**: Define element builder, pipeline builder, and builder factory interfaces.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-012 | Create `IElementBuilder` interface (takes full `PipelineConfig` + index) | ☐ | |
| TASK-013 | Create `IPipelineBuilder` interface (build + get_pipeline) | ☐ | |
| TASK-014 | Create `IBuilderFactory` interface (creates builders by GStreamer type name) | ☐ | |

**File:** `core/include/engine/core/builders/ielement_builder.hpp`

```cpp
#pragma once
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>

namespace engine::core::builders {

/**
 * @brief Builds a single GStreamer element from the full pipeline config.
 * @param config Full PipelineConfig — builder accesses its relevant section.
 * @param index  Index into a repeated section (e.g., processing.elements[index]).
 * @return Raw GstElement* — ownership transfers to GstBin after gst_bin_add().
 */
class IElementBuilder {
public:
    virtual ~IElementBuilder() = default;
    virtual GstElement* build(const engine::core::config::PipelineConfig& config,
                              int index = 0) = 0;
};

} // namespace engine::core::builders
```

**File:** `core/include/engine/core/builders/ipipeline_builder.hpp`

```cpp
#pragma once
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>
#include <glib.h>

namespace engine::core::builders {

/**
 * @brief Builds the complete GStreamer pipeline from config.
 */
class IPipelineBuilder {
public:
    virtual ~IPipelineBuilder() = default;
    virtual bool build(const engine::core::config::PipelineConfig& config,
                       GMainLoop* main_loop) = 0;
    virtual GstElement* get_pipeline() const = 0;
};

} // namespace engine::core::builders
```

**File:** `core/include/engine/core/builders/ibuilder_factory.hpp`

```cpp
#pragma once
#include "engine/core/builders/ielement_builder.hpp"
#include <memory>
#include <string>

namespace engine::core::builders {

/**
 * @brief Factory that creates element builders by GStreamer type name.
 */
class IBuilderFactory {
public:
    virtual ~IBuilderFactory() = default;
    virtual std::unique_ptr<IElementBuilder> create(const std::string& type) = 0;
};

} // namespace engine::core::builders
```

### Phase 4 — Pipeline Interfaces

**GOAL-004**: Define pipeline state enum and lifecycle manager interface.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-015 | Create `PipelineState` enum (Uninitialized, Ready, Playing, Paused, Stopped, Error) | ☐ | |
| TASK-016 | Create `IPipelineManager` interface (initialize, start, stop, pause, resume, get_state) | ☐ | |

**File:** `core/include/engine/core/pipeline/pipeline_state.hpp`

```cpp
#pragma once

namespace engine::core::pipeline {

enum class PipelineState {
    Uninitialized,
    Ready,
    Playing,
    Paused,
    Stopped,
    Error
};

} // namespace engine::core::pipeline
```

**File:** `core/include/engine/core/pipeline/ipipeline_manager.hpp`

```cpp
#pragma once
#include "engine/core/config/config_types.hpp"
#include "engine/core/pipeline/pipeline_state.hpp"

namespace engine::core::pipeline {

/**
 * @brief Top-level pipeline lifecycle manager.
 * Used by app/ to initialize, start, stop, and query the pipeline.
 */
class IPipelineManager {
public:
    virtual ~IPipelineManager() = default;
    virtual bool initialize(const engine::core::config::PipelineConfig& config) = 0;
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual bool pause() = 0;
    virtual bool resume() = 0;
    virtual PipelineState get_state() const = 0;
};

} // namespace engine::core::pipeline
```

### Phase 5 — Handler & Eventing Interfaces

**GOAL-005**: Define event handler base interface and event type constants.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-017 | Create `IHandler` base interface (no static members, no infrastructure types) | ☐ | |
| TASK-018 | Create `event_types.hpp` constants (ON_EOS, ON_DETECT, ON_STATE_CHANGE, ON_ERROR) | ☐ | |

**File:** `core/include/engine/core/handlers/ihandler.hpp`

```cpp
#pragma once
#include <string>
#include <vector>

namespace engine::core::handlers {

/**
 * @brief Base interface for event handlers (pad probes, signal handlers).
 * Concrete handlers in pipeline/ receive dependencies via constructor injection.
 * NO static members, NO infrastructure types.
 */
class IHandler {
public:
    virtual ~IHandler() = default;
    virtual bool initialize(const std::string& config_json) = 0;
    virtual void destroy() = 0;
    virtual std::string get_handler_name() const = 0;
    virtual std::vector<std::string> get_supported_event_types() const = 0;
};

} // namespace engine::core::handlers
```

**File:** `core/include/engine/core/eventing/event_types.hpp`

```cpp
#pragma once
#include <string_view>

namespace engine::core::eventing {

/** @brief End-of-stream event key emitted from GstBus EOS messages. */
inline constexpr std::string_view ON_EOS = "on_eos";

/** @brief Detection event from pad probe on inference output. */
inline constexpr std::string_view ON_DETECT = "on_detect";

/** @brief State change event from GstBus state-changed messages. */
inline constexpr std::string_view ON_STATE_CHANGE = "on_state_change";

/** @brief Error event from GstBus error messages. */
inline constexpr std::string_view ON_ERROR = "on_error";

} // namespace engine::core::eventing
```

### Phase 6 — Messaging, Storage, Recording, Runtime Interfaces

**GOAL-006**: Define interfaces for external services and runtime control.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-019 | Create `IMessageProducer` interface (connect, publish, disconnect) | ☐ | |
| TASK-020 | Create `IStorageManager` interface (initialize, save, save_file, remove) | ☐ | |
| TASK-021 | Create `ISmartRecordController` interface (start_recording, stop_recording) | ☐ | |
| TASK-022 | Create `IRuntimeParamManager` interface (set_param, get_param) | ☐ | |

**File:** `core/include/engine/core/messaging/imessage_producer.hpp`

```cpp
#pragma once
#include <string>

namespace engine::core::messaging {

/**
 * @brief Publishes messages to an external broker (Redis Streams, Kafka, etc.).
 */
class IMessageProducer {
public:
    virtual ~IMessageProducer() = default;
    virtual bool connect(const std::string& host, int port,
                         const std::string& channel = "") = 0;
    virtual bool publish(const std::string& channel,
                         const std::string& message) = 0;
    virtual bool publish(const std::string& channel,
                         const std::string& key,
                         const std::string& value) = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;
};

} // namespace engine::core::messaging
```

**File:** `core/include/engine/core/storage/istorage_manager.hpp`

```cpp
#pragma once
#include <string>
#include <cstddef>

namespace engine::core::storage {

/**
 * @brief Manages saving/loading files (local filesystem, S3, etc.).
 */
class IStorageManager {
public:
    virtual ~IStorageManager() = default;
    virtual bool initialize(const std::string& base_path) = 0;
    virtual bool save(const std::string& dest_path,
                      const void* data, size_t size) = 0;
    virtual bool save_file(const std::string& src_path,
                           const std::string& dest_path) = 0;
    virtual bool remove(const std::string& path) = 0;
};

} // namespace engine::core::storage
```

**File:** `core/include/engine/core/recording/ismart_record_controller.hpp`

```cpp
#pragma once
#include <gst/gst.h>

namespace engine::core::recording {

/**
 * @brief Controls NvDsSR (Smart Record) on nvmultiurisrcbin.
 */
class ISmartRecordController {
public:
    virtual ~ISmartRecordController() = default;
    virtual bool start_recording(int source_id, int duration_sec = 0) = 0;
    virtual bool stop_recording(int source_id) = 0;
};

} // namespace engine::core::recording
```

**File:** `core/include/engine/core/runtime/iruntime_param_manager.hpp`

```cpp
#pragma once
#include <string>

namespace engine::core::runtime {

/**
 * @brief Allows runtime modification of GStreamer element properties.
 */
class IRuntimeParamManager {
public:
    virtual ~IRuntimeParamManager() = default;
    virtual bool set_param(const std::string& element_id,
                           const std::string& property,
                           const std::string& value) = 0;
    virtual std::string get_param(const std::string& element_id,
                                  const std::string& property) = 0;
};

} // namespace engine::core::runtime
```

### Phase 7 — Utility Headers + Source

**GOAL-007**: Provide RAII helpers for GStreamer objects and logger infrastructure.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-023 | Create `gst_utils.hpp` with RAII type aliases (GstElementPtr, GstCapsPtr, etc.) | ☐ | |
| TASK-024 | Create `logger.hpp` with LOG_* macros wrapping spdlog | ☐ | |
| TASK-025 | Create `spdlog_logger.hpp` + `spdlog_logger.cpp` (initialize_logger function) | ☐ | |

**File:** `core/include/engine/core/utils/gst_utils.hpp`

```cpp
#pragma once
#include <gst/gst.h>
#include <glib.h>
#include <memory>

namespace engine::core::utils {

/// @brief RAII guard for GstElement NOT yet added to a bin.
using GstElementPtr = std::unique_ptr<GstElement, decltype(&gst_object_unref)>;

inline GstElementPtr make_gst_element(const char* factory, const char* name) {
    return GstElementPtr(gst_element_factory_make(factory, name),
                         gst_object_unref);
}

using GstCapsPtr    = std::unique_ptr<GstCaps,    decltype(&gst_caps_unref)>;
using GstPadPtr     = std::unique_ptr<GstPad,     decltype(&gst_object_unref)>;
using GstBusPtr     = std::unique_ptr<GstBus,     decltype(&gst_object_unref)>;
using GMainLoopPtr  = std::unique_ptr<GMainLoop,  decltype(&g_main_loop_unref)>;
using GErrorPtr     = std::unique_ptr<GError,     decltype(&g_error_free)>;
using GCharPtr      = std::unique_ptr<gchar,      decltype(&g_free)>;

} // namespace engine::core::utils
```

**File:** `core/include/engine/core/utils/logger.hpp`

```cpp
#pragma once
#include <spdlog/spdlog.h>

// Global macros — NEVER use std::cout or printf in library code
#define LOG_T(...) SPDLOG_TRACE(__VA_ARGS__)
#define LOG_D(...) SPDLOG_DEBUG(__VA_ARGS__)
#define LOG_I(...) SPDLOG_INFO(__VA_ARGS__)
#define LOG_W(...) SPDLOG_WARN(__VA_ARGS__)
#define LOG_E(...) SPDLOG_ERROR(__VA_ARGS__)
#define LOG_C(...) SPDLOG_CRITICAL(__VA_ARGS__)
```

**File:** `core/include/engine/core/utils/spdlog_logger.hpp`

```cpp
#pragma once
#include <string>

namespace engine::core::utils {

/**
 * @brief Initializes spdlog with console + optional file sink.
 * @param log_level  "TRACE"|"DEBUG"|"INFO"|"WARN"|"ERROR"|"CRITICAL"
 * @param log_file   Path to log file; empty = console only.
 */
void initialize_logger(const std::string& log_level = "INFO",
                       const std::string& log_file = "");

} // namespace engine::core::utils
```

**File:** `core/src/utils/spdlog_logger.cpp`

```cpp
#include "engine/core/utils/spdlog_logger.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <vector>
#include <memory>

namespace engine::core::utils {

void initialize_logger(const std::string& log_level,
                       const std::string& log_file)
{
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    if (!log_file.empty()) {
        sinks.push_back(
            std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file, true));
    }

    auto logger = std::make_shared<spdlog::logger>("vms_engine", sinks.begin(), sinks.end());
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");

    if (log_level == "TRACE")         logger->set_level(spdlog::level::trace);
    else if (log_level == "DEBUG")    logger->set_level(spdlog::level::debug);
    else if (log_level == "WARN")     logger->set_level(spdlog::level::warn);
    else if (log_level == "ERROR")    logger->set_level(spdlog::level::err);
    else if (log_level == "CRITICAL") logger->set_level(spdlog::level::critical);
    else                              logger->set_level(spdlog::level::info);

    spdlog::set_default_logger(logger);
    spdlog::flush_every(std::chrono::seconds(3));
}

} // namespace engine::core::utils
```

### Phase 8 — CMakeLists.txt

**GOAL-008**: Update `core/CMakeLists.txt` to compile `vms_engine_core` independently.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-026 | Update `core/CMakeLists.txt` with spdlog_logger.cpp source and public includes | ☐ | |

```cmake
# core/CMakeLists.txt
add_library(vms_engine_core STATIC
    src/utils/spdlog_logger.cpp
)

target_include_directories(vms_engine_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PUBLIC ${DEEPSTREAM_INCLUDE_DIRS}
)

target_link_libraries(vms_engine_core
    PUBLIC spdlog::spdlog
    PUBLIC PkgConfig::GLIB2
    PUBLIC PkgConfig::GST
)

set_target_properties(vms_engine_core PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)
```

---

## Design Decisions

### Why single `config_types.hpp` instead of separate files?

In lantanav2, config was split across 10+ headers (`source_config.hpp`, `muxer_config.hpp`, `inference_config.hpp`, `tracker_config.hpp`, etc.) with `std::variant` wrappers to support multiple backends. This created circular dependency chains and excessive header coupling. vms-engine is DeepStream-native — no backend variants. All structs are plain, flat, and directly map to the YAML schema. One header keeps everything discoverable and eliminates cross-file dependencies.

### Why `has_queue` + `QueueConfig` instead of `std::optional<QueueConfig>`?

The YAML uses three patterns: `queue: {}` (defaults), `queue: { max_size_buffers: 20 }` (overrides), or no queue key (skip). Using `has_queue` boolean + always-populated `QueueConfig` simplifies builder logic:

```cpp
if (elem_cfg.has_queue) {
    // merge elem_cfg.queue with config.queue_defaults, then build queue element
}
```

---

## 3. Alternatives

- **ALT-001**: Separate config headers per element type (rejected — causes circular dependencies, as seen in lantanav2).
- **ALT-002**: Use `std::optional<QueueConfig>` for inline queues (rejected — `has_queue` boolean is simpler for builder conditionals).
- **ALT-003**: Include DeepStream SDK headers in core (rejected — violates layering; core only uses `<gst/gst.h>` forward-declares).

---

## 4. Dependencies

- **DEP-001**: Plan 01 completed — project scaffold and CMake build system in place.
- **DEP-002**: spdlog v1.14.1 — fetched via FetchContent in root `CMakeLists.txt`.
- **DEP-003**: GStreamer 1.0 — provided by DeepStream SDK container.
- **DEP-004**: GLib 2.0 — provided by DeepStream SDK container.
- **DEP-005**: `docs/configs/deepstream_default.yml` — canonical YAML schema for config struct design.

---

## 5. Files

| ID | File Path | Description |
|----|-----------|-------------|
| FILE-001 | `core/include/engine/core/config/config_types.hpp` | All config structs (single header) |
| FILE-002 | `core/include/engine/core/config/iconfig_parser.hpp` | IConfigParser interface |
| FILE-003 | `core/include/engine/core/config/iconfig_validator.hpp` | IConfigValidator + ValidationResult |
| FILE-004 | `core/include/engine/core/builders/ielement_builder.hpp` | IElementBuilder interface |
| FILE-005 | `core/include/engine/core/builders/ipipeline_builder.hpp` | IPipelineBuilder interface |
| FILE-006 | `core/include/engine/core/builders/ibuilder_factory.hpp` | IBuilderFactory interface |
| FILE-007 | `core/include/engine/core/pipeline/pipeline_state.hpp` | PipelineState enum |
| FILE-008 | `core/include/engine/core/pipeline/ipipeline_manager.hpp` | IPipelineManager interface |
| FILE-009 | `core/include/engine/core/handlers/ihandler.hpp` | IHandler base interface |
| FILE-010 | `core/include/engine/core/eventing/event_types.hpp` | Event type constants |
| FILE-011 | `core/include/engine/core/messaging/imessage_producer.hpp` | IMessageProducer interface |
| FILE-012 | `core/include/engine/core/storage/istorage_manager.hpp` | IStorageManager interface |
| FILE-013 | `core/include/engine/core/recording/ismart_record_controller.hpp` | ISmartRecordController interface |
| FILE-014 | `core/include/engine/core/runtime/iruntime_param_manager.hpp` | IRuntimeParamManager interface |
| FILE-015 | `core/include/engine/core/utils/gst_utils.hpp` | RAII helpers (header-only) |
| FILE-016 | `core/include/engine/core/utils/logger.hpp` | LOG_* macros |
| FILE-017 | `core/include/engine/core/utils/spdlog_logger.hpp` | initialize_logger declaration |
| FILE-018 | `core/src/utils/spdlog_logger.cpp` | initialize_logger implementation |
| FILE-019 | `core/CMakeLists.txt` | Build config for vms_engine_core |

---

## 6. Testing & Verification

- **TEST-001**: Compile `vms_engine_core` independently — `cmake --build build --target vms_engine_core -- -j5`.
- **TEST-002**: Full build succeeds — `cmake --build build -- -j5`.
- **TEST-003**: Run stub binary — `./build/bin/vms_engine`.
- **TEST-004**: No backend dependencies in core — `grep -r "backends\|deepstream\|dlstreamer" core/include/ core/src/ && echo "FAIL" || echo "PASS"`.
- **TEST-005**: No lantana references — `grep -r "lantana" core/include/ core/src/ && echo "FAIL" || echo "PASS"`.
- **TEST-006**: All headers have `engine::` namespace — `grep -rL "engine::" core/include/engine/ | head -20`.
- **TEST-007**: Full cmake configuration — configure, build core only, then build all.

---

## 7. Risks & Assumptions

- **RISK-001**: Adding new DeepStream element types later may require extending `ProcessingElementConfig` or `OutputElementConfig` with additional flat fields; mitigated by keeping structs open to extension.
- **RISK-002**: Single `config_types.hpp` may grow large; mitigated by clear section separators and struct naming conventions.
- **ASSUMPTION-001**: DeepStream SDK 8.0 is the only pipeline backend — no multi-backend variant support needed.
- **ASSUMPTION-002**: `<gst/gst.h>` is the only framework header allowed in core interfaces.
- **ASSUMPTION-003**: All config struct defaults match `docs/configs/deepstream_default.yml` values.

---

## 8. Related Specifications

- [Plan 01 — Project Scaffold](01_project_scaffold.md)
- [Plan 03 — Pipeline Layer](03_pipeline_layer.md)
- [Canonical YAML Schema](../../configs/deepstream_default.yml)
- [RAII Guide](../../docs/architecture/RAII.md)
- [Core Interfaces Doc](../../docs/architecture/deepstream/02_core_interfaces.md)
- [AGENTS.md](../../AGENTS.md)
