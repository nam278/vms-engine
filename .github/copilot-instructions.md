# GitHub Copilot Instructions — VMS Engine

## Priority Guidelines

When generating code for this repository:

1. **Version Compatibility**: C++17 strictly. Never use C++20/23 features. DeepStream SDK 8.0 APIs only.
2. **Architecture Compliance**: Enforce the dependency rule — inner layers never import outer layers.
3. **Interface-First**: Always define/extend an `I*` interface in `core/` before implementing.
4. **Namespace**: All code uses `engine::` namespace (NOT `lantana::` — that is the old project).
5. **Config-Driven**: Pipeline behavior comes from `PipelineConfig`; builders receive the full config, not slices.
6. **Codebase Patterns First**: When in doubt, scan existing files for patterns before inventing new ones.

---

## Technology Stack

All versions detected from `CMakeLists.txt` and `Dockerfile`.

| Technology        | Version         | Source                                                     |
| ----------------- | --------------- | ---------------------------------------------------------- |
| C++               | **C++17**       | `set(CMAKE_CXX_STANDARD 17)` in root CMakeLists.txt        |
| CMake             | **3.16+**       | `cmake_minimum_required(VERSION 3.16 FATAL_ERROR)`         |
| NVIDIA DeepStream | **8.0**         | `FROM nvcr.io/nvidia/deepstream:8.0-gc-triton-devel`       |
| GStreamer         | **1.14+**       | `pkg_check_modules(GST gstreamer-1.0>=1.14)`               |
| spdlog            | **1.14.1**      | `FetchContent GIT_TAG v1.14.1`                             |
| yaml-cpp          | **0.8.0**       | `FetchContent GIT_TAG 0.8.0`                               |
| hiredis           | **1.3.0**       | `FetchContent GIT_TAG v1.3.0`                              |
| nlohmann/json     | **3.11.3**      | `FetchContent GIT_TAG v3.11.3`                             |
| librdkafka        | **2.3.0**       | `FetchContent GIT_TAG v2.3.0` (Kafka producer)             |
| Build generator   | **Ninja**       | Preferred; fallback to Make                                |

Do **not** use features beyond these versions.

---

## Architecture

### Layer Dependency Map

```
app/          → depends on: core, pipeline, domain, infrastructure
pipeline/     → depends on: core ONLY
domain/       → depends on: core ONLY
infrastructure/ → depends on: core ONLY
core/         → depends on: std library + GStreamer forward-declares ONLY
```

**Violations to avoid:**

- `#include "engine/pipeline/..."` inside `core/` → ❌ FORBIDDEN
- `#include "engine/infrastructure/..."` inside `pipeline/` → ❌ FORBIDDEN
- `#include "engine/domain/..."` inside `pipeline/` → ❌ FORBIDDEN
- Using `lantana::` namespace anywhere → ❌ WRONG (old project name)

### Layer Directory Reference

```
core/include/engine/core/
    builders/       # IElementBuilder, IPipelineBuilder, IBuilderFactory
    config/         # config_types.hpp (PipelineConfig), IConfigParser, IConfigValidator
    eventing/       # event_types.hpp (ON_EOS, ON_DETECT constants)
    messaging/      # IMessageProducer
    pipeline/       # IPipelineManager, PipelineState
    storage/        # IStorageManager
    runtime/        # IParamManager
    utils/          # logger.hpp, gst_utils.hpp, spdlog_logger.hpp

pipeline/
    include/engine/pipeline/
        pipeline_builder.hpp      # Concrete IPipelineBuilder (5-phase build)
        pipeline_manager.hpp      # Concrete IPipelineManager (lifecycle)
        builders/                 # Element builders (SourceBuilder, InferBuilder, ...)
        probes/                   # ProbeHandlerManager, SmartRecord, CropObject, ...
    src/                          # Element builder implementations

infrastructure/
    config_parser/                # YamlConfigParser (IConfigParser)
    messaging/                    # RedisStreamProducer (IMessageProducer)
    storage/                      # File/S3 implementations (IStorageManager)
    rest_api/                     # REST adapter

domain/include/engine/domain/
    event_processor.hpp           # IEventProcessor, DetectionResult, FrameEvent
    metadata_parser.hpp           # IMetadataParser
    runtime_param_rules.hpp       # Runtime parameter rules
```

### Interface-First Pattern

Define `I*` in `core/` before implementing in any other layer:

```cpp
// ✅ core/include/engine/core/messaging/imessage_producer.hpp
namespace engine::core::messaging {
class IMessageProducer {
public:
    virtual ~IMessageProducer() = default;
    virtual bool connect(const std::string& host, int port,
                         const std::string& channel = "") = 0;
    virtual bool publish(const std::string& channel, const std::string& message) = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;
};
} // namespace engine::core::messaging

// ✅ infrastructure/messaging/include/engine/infrastructure/messaging/redis_stream_producer.hpp
#include "engine/core/messaging/imessage_producer.hpp"
namespace engine::infrastructure::messaging {
class RedisStreamProducer : public engine::core::messaging::IMessageProducer {
    // ...
};
} // namespace engine::infrastructure::messaging
```

---

## Naming Conventions

```cpp
// Namespaces — snake_case
namespace engine::core::pipeline { }
namespace engine::pipeline::builders { }

// Interfaces — I + PascalCase
class IPipelineManager { };
class IElementBuilder { };
class IMessageProducer { };

// Concrete classes — PascalCase (no prefix)
class PipelineManager : public IPipelineManager { };
class SourceBuilder : public IElementBuilder { };

// Methods — snake_case
virtual bool initialize(PipelineConfig& config) = 0;
GstElement* build(const PipelineConfig& config, int index = 0);

// Member variables — snake_case with trailing underscore
GstElement* pipeline_ = nullptr;
PipelineConfig config_;
std::unordered_map<std::string, GstElement*> tails_;

// Constants / enum values — PascalCase for values
enum class PipelineState { Uninitialized, Ready, Playing, Paused, Stopped, Error };

// Files — snake_case
// pipeline_manager.hpp, source_builder.cpp, yaml_config_parser.hpp
```

---

## Doxygen Comment Requirements

Use `/** ... */` on all public interfaces, public methods, and non-obvious structs.

```cpp
/**
 * @brief Builds a single GStreamer element from the full pipeline config.
 * @param config Full PipelineConfig — builder accesses its relevant section.
 * @param index  Index into a repeated section (e.g., processing.elements[index]).
 * @return Raw GstElement* — ownership transfers to GstBin after gst_bin_add().
 */
class IElementBuilder {
   public:
    virtual GstElement* build(const engine::core::config::PipelineConfig& config,
                              int index = 0) = 0;
};

/** @brief End-of-stream event key emitted from GstBus EOS messages. */
inline constexpr std::string_view ON_EOS = "on_eos";
```

Keep comments focused on contract/invariants/ownership, not line-by-line narration.

---

## PipelineConfig Structure

All builders receive `const engine::core::config::PipelineConfig&` — the full root config.

```cpp
// core/include/engine/core/config/config_types.hpp
struct PipelineConfig {
    std::string            version;
    PipelineMetaConfig     pipeline;        // id, name, log_level, gst_log_level
    QueueConfig            queue_defaults;  // default for every queue: {}

    SourcesConfig          sources;         // nvmultiurisrcbin + cameras[]
    ProcessingConfig       processing;      // elements[] (nvinfer, nvtracker, …)
    VisualsConfig          visuals;         // enable + elements[] (tiler, osd)
    std::vector<OutputConfig>       outputs;        // each has id, type, elements[]
    std::vector<EventHandlerConfig> event_handlers; // pad probes
    std::optional<MessagingConfig>  messaging;      // optional Redis/Kafka broker
};

// Builder access patterns:
// config.sources                         — SourcesConfig (single block)
// config.processing.elements[index]      — ProcessingElementConfig
// config.visuals.elements[index]         — VisualsElementConfig
// config.outputs[i].elements[j]          — OutputElementConfig
// config.event_handlers[i]              — EventHandlerConfig
```

**No `std::variant` over backend types** — vms-engine is DeepStream-native only.

---

## Element Builder Pattern

Confirmed from `pipeline/src/builders/infer_builder.cpp` and `source_builder.cpp`:

```cpp
// Builder header: pipeline/include/engine/pipeline/builders/my_builder.hpp
#pragma once
#include "engine/core/builders/ielement_builder.hpp"
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>

namespace engine::pipeline::builders {

/**
 * @brief Builds nvmyelement from pipeline processing config.
 */
class MyBuilder : public engine::core::builders::IElementBuilder {
   public:
    explicit MyBuilder(GstElement* bin) : bin_(bin) {}
    GstElement* build(const engine::core::config::PipelineConfig& config,
                      int index = 0) override;
   private:
    GstElement* bin_ = nullptr;
};

} // namespace engine::pipeline::builders
```

```cpp
// Builder implementation — RAII guard, g_object_set, gst_bin_add, release()
#include "engine/pipeline/builders/my_builder.hpp"
#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::builders {

GstElement* MyBuilder::build(
    const engine::core::config::PipelineConfig& config, int index)
{
    const auto& elem_cfg = config.processing.elements[index];
    const auto& id = elem_cfg.id;

    // ✅ RAII guard auto-unrefs on all failure paths
    auto elem = engine::core::utils::make_gst_element("nvmyelement", id.c_str());
    if (!elem) {
        LOG_E("Failed to create nvmyelement '{}'", id);
        return nullptr;
    }

    // ✅ Always use static_cast<>; always terminate g_object_set with nullptr
    g_object_set(G_OBJECT(elem.get()),
                 "gpu-id",     static_cast<gint>(elem_cfg.gpu_id),
                 "batch-size", static_cast<gint>(elem_cfg.batch_size),
                 nullptr);

    if (!gst_bin_add(GST_BIN(bin_), elem.get())) {
        LOG_E("Failed to add nvmyelement '{}' to bin", id);
        return nullptr;  // auto-unref
    }

    LOG_I("Built nvmyelement '{}' (batch={})", id, elem_cfg.batch_size);
    return elem.release();  // bin owns — disarm guard
}

} // namespace engine::pipeline::builders
```

---

## GStreamer API Patterns

```cpp
// ✅ Always terminate g_object_set with nullptr
g_object_set(G_OBJECT(element), "property-name", value, nullptr);

// ✅ Always use static_cast<> for GLib types
static_cast<gint>(my_int), static_cast<guint>(my_uint),
static_cast<gboolean>(my_bool), static_cast<gdouble>(my_double)

// ✅ Dynamic pad connections (nvmultiurisrcbin → muxer)
g_signal_connect(source_bin, "pad-added",
                 G_CALLBACK(on_pad_added_callback), muxer);

// ✅ Probe pattern — attach to element's static pad
GstPad* pad = gst_element_get_static_pad(element, "src");
gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, probe_callback, user_data,
    [](gpointer ud) { delete static_cast<MyHandler*>(ud); });
gst_object_unref(pad);  // unref after add_probe

// ✅ Inside pad probe callback
static GstPadProbeReturn probe_callback(GstPad*, GstPadProbeInfo* info, gpointer) {
    GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
    NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buf);
    for (NvDsMetaList* l = batch_meta->frame_meta_list; l; l = l->next) {
        NvDsFrameMeta* fm = static_cast<NvDsFrameMeta*>(l->data);
        for (NvDsMetaList* lo = fm->obj_meta_list; lo; lo = lo->next) {
            NvDsObjectMeta* obj = static_cast<NvDsObjectMeta*>(lo->data);
            // obj->class_id, obj->confidence, obj->object_id, obj->rect_params
        }
    }
    return GST_PAD_PROBE_OK;
}
```

---

## YAML Config Conventions

- YAML keys use `snake_case` — parser maps to GStreamer's `kebab-case` internally.
- All enum values → **integers** in YAML (never strings).
- Explicit `queue: {}` inserts a leaky queue after that element.

```yaml
version: "1.0.0"
pipeline:
  id: my_pipeline
  log_level: INFO        # DEBUG | INFO | WARN | ERROR

queue_defaults:
  max_size_buffers: 10
  leaky: 2               # 0=none, 1=upstream, 2=downstream

sources:
  type: nvmultiurisrcbin
  # NOTE: ip_address is NOT configurable — DS8 ip-address setter causes SIGSEGV.
  # rest_api_port: 0=disable CivetWeb REST API; >0 enables on that port (e.g. 9000)
  rest_api_port: 0
  max_batch_size: 4
  mode: 0                # 0=video, 1=audio
  gpu_id: 0
  cameras:
    - id: camera-01
      uri: "rtsp://host/stream"
  smart_record: 2        # int enum: 0=disable, 1=cloud, 2=multi
  smart_rec_dir_path: "/opt/engine/data/rec"

processing:
  elements:
    - id: pgie
      type: nvinfer
      role: primary_inference
      config_file: "/opt/engine/data/components/pgie/config.yml"
      process_mode: 1    # int: 1=primary, 2=secondary
      batch_size: 4
      queue: {}
    - id: tracker
      type: nvtracker
      ll_lib_file: "/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so"
      ll_config_file: "/opt/engine/data/config/config_tracker_NvDCF_perf.yml"
      tracker_width: 640
      tracker_height: 640
      compute_hw: 1      # int: 0=default, 1=GPU, 2=VIC
      queue: {}

event_handlers:
  - id: smart_record
    enable: true
    type: on_detect
    probe_element: tracker
    source_element: nvmultiurisrcbin0
    trigger: smart_record
    label_filter: [car, person, truck]
```

---

## DeepStream Plugin Properties — g_object_set Reference

### ⚠️ DS8 Critical Notes
- **NEVER set `ip-address`** on nvmultiurisrcbin — triggers SIGSEGV in DS8.0.
- `port` is a **string** property: `"0"` = disable REST API, `"9000"` = enable.
- All properties use `static_cast<gint/guint/gboolean/gdouble>()` in actual source.

### nvmultiurisrcbin

```cpp
// Group 1 — direct
const std::string port_str = std::to_string(cfg.rest_api_port);
g_object_set(G_OBJECT(bin), "port", port_str.c_str(),
    "max-batch-size", static_cast<gint>(cfg.max_batch_size),
    "mode",           static_cast<gint>(cfg.mode), nullptr);

// Group 2 — nvurisrcbin passthrough
g_object_set(G_OBJECT(bin), "gpu-id", static_cast<gint>(cfg.gpu_id),
    "select-rtp-protocol",     static_cast<gint>(cfg.select_rtp_protocol),  // 4=TCP
    "rtsp-reconnect-interval", static_cast<gint>(cfg.rtsp_reconnect_interval),
    "rtsp-reconnect-attempts", static_cast<gint>(cfg.rtsp_reconnect_attempts),
    "latency",                 static_cast<guint>(cfg.latency),
    "drop-pipeline-eos",       static_cast<gboolean>(cfg.drop_pipeline_eos),
    "disable-audio",           static_cast<gboolean>(cfg.disable_audio),
    "file-loop",               static_cast<gboolean>(cfg.file_loop), nullptr);

// Group 3 — nvstreammux passthrough
g_object_set(G_OBJECT(bin), "width",  static_cast<gint>(cfg.width),
    "height", static_cast<gint>(cfg.height),
    "batched-push-timeout", static_cast<gint>(cfg.batched_push_timeout),  // µs
    "live-source", static_cast<gboolean>(cfg.live_source), nullptr);

// Smart Record (only if smart_record > 0)
g_object_set(G_OBJECT(bin), "smart-record", static_cast<gint>(cfg.smart_record),
    "smart-rec-dir-path",        cfg.smart_rec_dir_path.c_str(),
    "smart-rec-file-prefix",     cfg.smart_rec_file_prefix.c_str(),
    "smart-rec-cache",           static_cast<gint>(cfg.smart_rec_cache),
    "smart-rec-default-duration",static_cast<gint>(cfg.smart_rec_default_duration),
    "smart-rec-mode",            static_cast<gint>(cfg.smart_rec_mode),
    "smart-rec-container",       static_cast<gint>(cfg.smart_rec_container), nullptr);
```

### nvinfer (PGIE + SGIE)

```cpp
g_object_set(G_OBJECT(infer), "config-file-path", cfg.config_file.c_str(),
    "process-mode", static_cast<gint>(cfg.process_mode),  // 1=primary, 2=secondary
    "gpu-id",       static_cast<gint>(cfg.gpu_id), nullptr);
if (cfg.batch_size > 0)
    g_object_set(G_OBJECT(infer), "batch-size", static_cast<gint>(cfg.batch_size), nullptr);
if (cfg.unique_id > 0)
    g_object_set(G_OBJECT(infer), "unique-id", static_cast<gint>(cfg.unique_id), nullptr);
if (cfg.interval > 0)
    g_object_set(G_OBJECT(infer), "interval", static_cast<gint>(cfg.interval), nullptr);

// SGIE only — process_mode == 2
// NOTE: GObject prop is "infer-on-gie-id" (config-file uses "operate-on-gie-id")
//       operate_on_class_ids: colon-separated "0:2:3" for GObject
if (cfg.process_mode == 2) {
    g_object_set(G_OBJECT(infer), "infer-on-gie-id",
                 static_cast<gint>(cfg.operate_on_gie_id), nullptr);
    if (!cfg.operate_on_class_ids.empty())
        g_object_set(G_OBJECT(infer), "infer-on-class-ids",
                     cfg.operate_on_class_ids.c_str(), nullptr);
}
```

### nvtracker

```cpp
g_object_set(G_OBJECT(tracker),
    "ll-lib-file",      cfg.ll_lib_file.c_str(),
    "ll-config-file",   cfg.ll_config_file.c_str(),
    "tracker-width",    static_cast<gint>(cfg.tracker_width),
    "tracker-height",   static_cast<gint>(cfg.tracker_height),
    "gpu-id",           static_cast<gint>(cfg.gpu_id),
    "compute-hw",       static_cast<gint>(cfg.compute_hw),  // 0=default,1=GPU,2=VIC
    "display-tracking-id", static_cast<gboolean>(cfg.display_tracking_id), nullptr);
```

### nvdsosd

```cpp
g_object_set(G_OBJECT(osd),
    "process-mode", static_cast<gint>(cfg.process_mode),  // 0=CPU,1=GPU,2=HW
    "display-bbox", static_cast<gboolean>(cfg.display_bbox),
    "display-text", static_cast<gboolean>(cfg.display_text),
    "gpu-id",       static_cast<gint>(cfg.gpu_id), nullptr);
```

### nvmultistreamtiler

```cpp
g_object_set(G_OBJECT(tiler),
    "rows",    static_cast<guint>(cfg.rows),
    "columns", static_cast<guint>(cfg.columns),
    "width",   static_cast<guint>(cfg.width),
    "height",  static_cast<guint>(cfg.height),
    "gpu-id",  static_cast<gint>(cfg.gpu_id), nullptr);
```

### nvdsanalytics

```cpp
g_object_set(G_OBJECT(analytics),
    "config-file",            cfg.config_file.c_str(),
    "gpu-id",                 static_cast<gint>(cfg.gpu_id),
    "enable-secondary-input", static_cast<gboolean>(cfg.enable_secondary_input), nullptr);
```

### nvmsgconv + nvmsgbroker

```cpp
// nvmsgconv
g_object_set(G_OBJECT(msgconv), "config", cfg.msgconv_config.c_str(),
    "payload-type", static_cast<gint>(cfg.payload_type),  // 0=DEEPSTREAM, 1=MINIMAL
    "comp-id",      static_cast<gint>(cfg.comp_id), nullptr);

// nvmsgbroker
g_object_set(G_OBJECT(broker), "proto-lib", cfg.proto_lib.c_str(),
    "conn-str", cfg.conn_str.c_str(), "topic", cfg.topic.c_str(),
    "config",   cfg.broker_config.c_str(), "sync", static_cast<gboolean>(FALSE), nullptr);
// proto-lib: /opt/nvidia/deepstream/deepstream/lib/libnvds_redis_proto.so
//            /opt/nvidia/deepstream/deepstream/lib/libnvds_kafka_proto.so
```

### nvv4l2h264enc / nvv4l2h265enc

```cpp
g_object_set(G_OBJECT(encoder), "bitrate",    static_cast<guint>(cfg.bitrate),
    "iframeinterval",  static_cast<guint>(cfg.iframeinterval),
    "preset-level",    static_cast<gint>(cfg.preset_level),    // 1=UltraFast..4=Medium
    "insert-sps-pps",  static_cast<gboolean>(TRUE),            // REQUIRED for RTSP
    "maxperf-enable",  static_cast<gboolean>(cfg.maxperf_enable), nullptr);
```

---

## Memory Management & RAII

> Full guide → `docs/architecture/RAII.md`

### Ownership Rules

| Resource                       | Cleanup                              |
| ------------------------------ | ------------------------------------ |
| `gst_element_factory_make()`   | `gst_object_unref()` until `gst_bin_add()` transfers ownership |
| `make_gst_element()` (RAII)    | auto-unref on scope exit; call `.release()` after `gst_bin_add()` |
| `gst_element_get_static_pad()` | `gst_object_unref()` when done       |
| `gst_caps_new_*()`             | `gst_caps_unref()` when done         |
| `gst_pipeline_get_bus()`       | `gst_object_unref()` when done       |
| `g_object_get(...gchar*...)`   | `g_free()` the string                |
| `NvDsBatchMeta / Frame / Obj`  | DO NOT FREE — pipeline owns          |

### RAII Type Aliases (from `core/include/engine/core/utils/gst_utils.hpp`)

```cpp
namespace engine::core::utils {
    using GstElementPtr = std::unique_ptr<GstElement, decltype(&gst_object_unref)>;
    inline GstElementPtr make_gst_element(const char* factory, const char* name);
    using GstCapsPtr    = std::unique_ptr<GstCaps,    decltype(&gst_caps_unref)>;
    using GstPadPtr     = std::unique_ptr<GstPad,     decltype(&gst_object_unref)>;
    using GstBusPtr     = std::unique_ptr<GstBus,     decltype(&gst_object_unref)>;
    using GMainLoopPtr  = std::unique_ptr<GMainLoop,  decltype(&g_main_loop_unref)>;
    using GErrorPtr     = std::unique_ptr<GError,     decltype(&g_error_free)>;
    using GCharPtr      = std::unique_ptr<gchar,      decltype(&g_free)>;
}
```

---

## Logging

```cpp
#include "engine/core/utils/logger.hpp"  // always include this

LOG_T("Trace: {}", detail);          // verbose debug (compiled out in Release)
LOG_D("Debug: element '{}' ready", id);
LOG_I("Info: pipeline started, {} sources", count);
LOG_W("Warning: field '{}' deprecated", name);
LOG_E("Error: Failed to link {} → {}", src_name, sink_name);
LOG_C("Critical: GStreamer init failed");
```

- **NEVER** use `std::cout`, `printf`, or raw `spdlog::` calls in library code.
- `SPDLOG_ACTIVE_LEVEL=0` in Debug builds → `LOG_T`/`LOG_D` produce real code.
- `SPDLOG_ACTIVE_LEVEL=2` in Release builds → `LOG_T`/`LOG_D` compile to no-ops.

---

## CMakeLists.txt Patterns

```cmake
add_library(vms_engine_mylayer STATIC
    src/my_file.cpp)

target_include_directories(vms_engine_mylayer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${DEEPSTREAM_INCLUDE_DIRS}
        ${GSTREAMER_INCLUDE_DIRS})

target_link_libraries(vms_engine_mylayer
    PUBLIC
        vms_engine_core           # Layer dependency → core only
    PRIVATE
        ${GSTREAMER_LIBRARIES})

set_target_properties(vms_engine_mylayer PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
```

---

## Include Path Conventions

```cpp
// ✅ Project headers — double-quote with full engine/ path
#include "engine/core/pipeline/ipipeline_manager.hpp"
#include "engine/core/config/config_types.hpp"
#include "engine/core/utils/logger.hpp"
#include "engine/core/utils/gst_utils.hpp"
#include "engine/pipeline/pipeline_manager.hpp"

// ✅ System / external headers — angle brackets
#include <gst/gst.h>
#include <nvds_meta.h>
#include <string>
#include <vector>
#include <memory>
```

Order: project headers first → system/external headers.

---

## Common Anti-Patterns

```cpp
// ❌ Old namespace (old project)
namespace lantana::core::pipeline { }

// ❌ Wrong include path
#include "lantana/core/builders/ibuilder_factory.hpp"  // ❌

// ❌ pipeline/ depending on infrastructure/
#include "engine/infrastructure/messaging/redis_stream_producer.hpp"  // inside pipeline/ ❌

// ❌ Missing nullptr terminator in g_object_set
g_object_set(G_OBJECT(el), "gpu-id", 0);  // BUG: missing nullptr ❌

// ❌ Missing static_cast in g_object_set
g_object_set(G_OBJECT(el), "gpu-id", 0, nullptr);  // No cast — risky ❌

// ❌ Builder receives config slice
GstElement* build(const ProcessingElementConfig& slice);  // WRONG ❌

// ❌ std::variant for backend types (lantanav2 pattern, removed here)
std::variant<DeepStreamOptions, DLStreamerOptions> backend_options;  // ❌

// ❌ std::cout in library code
std::cout << "Building element: " << name << std::endl;  // use LOG_I ❌

// ❌ C++20 features
auto result = std::ranges::find(vec, val);  // ranges = C++20 ❌
std::format("hello {}", name);              // format = C++20 ❌

// ❌ Setting ip-address on nvmultiurisrcbin — DS8 SIGSEGV
g_object_set(G_OBJECT(src), "ip-address", "0.0.0.0", nullptr);  // ❌ CRASH
```

---

## Architecture Documentation

Consult `docs/architecture/` for deep-dive implementation details:

| Document                                              | Topic                                                  |
| ----------------------------------------------------- | ------------------------------------------------------ |
| `deepstream/00_project_overview.md`                   | Tech stack, pipeline diagram, conventions              |
| `deepstream/02_core_interfaces.md`                    | All `I*` interfaces                                    |
| `deepstream/03_pipeline_building.md`                  | 5-phase build, `tails_` map pattern                   |
| `deepstream/04_linking_system.md`                     | Static/dynamic linking, `queue: {}` pattern            |
| `deepstream/05_configuration.md`                      | Full YAML schema, parser architecture                  |
| `deepstream/06_runtime_lifecycle.md`                  | GstBus, state machine, RTSP reconnect                  |
| `deepstream/07_event_handlers_probes.md`              | Pad probes, ProbeHandlerManager                        |
| `probes/smart_record_probe_handler.md`                | SmartRecord probe details                              |
| `probes/crop_object_handler.md`                       | CropObject probe details                               |
| `probes/class_id_namespacing_handler.md`              | Multi-GIE class_id collision resolution                |
| `probes/ext_proc_svc.md`                              | ExternalProcessorService (HTTP enrichment)             |
| `deepstream/10_rest_api.md`                           | DeepStream CivetWeb REST API, dynamic stream control   |
| `RAII.md`                                             | GStreamer / CUDA resource management (full guide)      |
| `CMAKE.md`                                            | Build system reference                                 |
