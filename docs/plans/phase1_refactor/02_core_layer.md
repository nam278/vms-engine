# Plan 02 — Core Layer (Interfaces, Config Types, Utilities)

> Build the `core/` layer from scratch for vms-engine.
> This is the foundation — **zero implementations**, only interfaces + config types + utils.
> All config types match the canonical YAML: [`docs/configs/deepstream_default.yml`](../../configs/deepstream_default.yml).

---

## Prerequisites

- Plan 01 completed (scaffold builds with placeholder sources)
- Build verification inside `vms-engine-dev` container

## Deliverables

- [ ] Config types in `core/include/engine/core/config/config_types.hpp`
- [ ] All `I*` interfaces under `core/include/engine/core/`
- [ ] RAII helpers in `core/include/engine/core/utils/gst_utils.hpp`
- [ ] Logger macros in `core/include/engine/core/utils/logger.hpp`
- [ ] `core/CMakeLists.txt` updated with all sources
- [ ] `vms_engine_core` library compiles independently

---

## Tasks

### 2.1 — Config Types (`config_types.hpp`)

Single header with all config structs matching the canonical YAML schema.
No separate files per element type — everything in one place.

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
    // NO output_queue — queue is per-element (queue: {} inline on each element)
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

    // nvdsanalytics properties
    // (config_file shared with nvinfer — same field)

    // Inline queue
    bool has_queue             = false;      ///< true if queue: {} present in YAML
    QueueConfig queue;
};

struct ProcessingConfig {
    std::vector<ProcessingElementConfig> elements;
    // NO output_queue — queue is per-element (queue: {} inline)
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
    // NO output_queue — queue is per-element (queue: {} inline)
};

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Outputs Block (arrays of flat element chains)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

struct OutputElementConfig {
    std::string id;
    std::string type;                        ///< GStreamer factory name

    // Common GStreamer properties (flat — parsed from YAML key-value)
    // Each element type has different properties; store as key-value pairs
    // for flexibility. Builders extract what they need.
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

### 2.2 — Config Interfaces

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

### 2.3 — Builder Interfaces

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

### 2.4 — Pipeline Interfaces

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

### 2.5 — Handler & Eventing Interfaces

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

### 2.6 — Messaging Interface

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

### 2.7 — Storage Interface

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

### 2.8 — Recording Interface

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

### 2.9 — Runtime Interfaces

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

### 2.10 — Utility Headers

**File:** `core/include/engine/core/utils/gst_utils.hpp`

RAII helpers for GStreamer objects — header-only, no `.cpp` needed.

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

### 2.11 — Source Files

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

### 2.12 — CMakeLists.txt Update

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

In lantanav2, config was split across 10+ headers (`source_config.hpp`, `muxer_config.hpp`, `inference_config.hpp`, `tracker_config.hpp`, `tiler_config.hpp`, `osd_config.hpp`, `sink_config.hpp`, `encoding_config.hpp`, ...) with `std::variant` wrappers to support multiple backends. This created:

- Circular dependency chains
- Excessive header coupling
- Hard-to-understand config flow

vms-engine is **DeepStream-native** — no backend variants. All structs are plain, flat, and directly map to the YAML schema. One header keeps everything discoverable and eliminates cross-file dependencies.

### Why `has_queue` + `QueueConfig` instead of `std::optional<QueueConfig>`?

The YAML uses three patterns:

- `queue: {}` → insert queue with defaults
- `queue: { max_size_buffers: 20 }` → insert queue with overrides
- _(no queue key)_ → no queue before this element

Using `has_queue` boolean + a `QueueConfig` struct (always populated with defaults) simplifies the builder logic:

```cpp
if (elem_cfg.has_queue) {
    // merge elem_cfg.queue with config.queue_defaults, then build queue element
}
```

### Where do individual element configs go?

nvinfer model configuration (`.txt` / `.yml` files) is separate from the pipeline YAML — it's referenced via `config_file` path. The YAML only contains GStreamer element properties.

---

## Verification

```bash
# Inside container: docker compose exec app bash
cd /opt/vms_engine

# 1. Configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream -G Ninja

# 2. Build core only
cmake --build build --target vms_engine_core -- -j5

# 3. Build all
cmake --build build -- -j5

# 4. Run stub
./build/bin/vms_engine

# 5. Check no backend dependencies in core
grep -r "backends\|deepstream\|dlstreamer" core/include/ core/src/ && echo "FAIL" || echo "PASS"

# 6. Check no lantana references
grep -r "lantana" core/include/ core/src/ && echo "FAIL" || echo "PASS"

# 7. Check all headers have engine:: namespace
grep -rL "engine::" core/include/engine/ | head -20
```

---

## Checklist

- [ ] `config_types.hpp` with all structs matching canonical YAML
- [ ] Config interfaces: `IConfigParser`, `IConfigValidator`
- [ ] Builder interfaces: `IElementBuilder`, `IPipelineBuilder`, `IBuilderFactory`
- [ ] Pipeline interfaces: `IPipelineManager`, `PipelineState`
- [ ] Handler interface: `IHandler` (no static members, no Redis coupling)
- [ ] Eventing: `event_types.hpp` constants
- [ ] Messaging: `IMessageProducer`
- [ ] Storage: `IStorageManager`
- [ ] Recording: `ISmartRecordController`
- [ ] Runtime: `IRuntimeParamManager`
- [ ] Utils: `gst_utils.hpp` (RAII), `logger.hpp` (macros), `spdlog_logger.hpp/.cpp`
- [ ] `core/CMakeLists.txt` updated
- [ ] `vms_engine_core` compiles independently
- [ ] Zero `lantana` references in core/
- [ ] Zero backend-specific includes in core/
