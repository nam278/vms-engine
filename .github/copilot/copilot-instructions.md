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

## 📚 Architecture Documentation

Before implementing anything DeepStream-related, consult the deep-dive docs in `docs/architecture/`:

| Document                                                                                                | Topic                                         |
| ------------------------------------------------------------------------------------------------------- | --------------------------------------------- |
| [`docs/architecture/deepstream/README.md`](../../docs/architecture/deepstream/README.md)                | Index & reading order                         |
| [`00_project_overview.md`](../../docs/architecture/deepstream/00_project_overview.md)                   | Tech stack, pipeline diagram, conventions     |
| [`02_core_interfaces.md`](../../docs/architecture/deepstream/02_core_interfaces.md)                     | All `I*` interfaces with `engine::` namespace |
| [`03_pipeline_building.md`](../../docs/architecture/deepstream/03_pipeline_building.md)                 | 5-phase build, `tails_` map pattern           |
| [`04_linking_system.md`](../../docs/architecture/deepstream/04_linking_system.md)                       | Static/dynamic linking, `queue: {}` pattern   |
| [`05_configuration.md`](../../docs/architecture/deepstream/05_configuration.md)                         | Full YAML schema, parser architecture         |
| [`06_runtime_lifecycle.md`](../../docs/architecture/deepstream/06_runtime_lifecycle.md)                 | GstBus, state machine, RTSP reconnect         |
| [`07_event_handlers_probes.md`](../../docs/architecture/deepstream/07_event_handlers_probes.md)         | HandlerManager, probes, built-in handlers     |
| [`08_analytics.md`](../../docs/architecture/deepstream/08_analytics.md)                                 | nvdsanalytics ROI / line crossing             |
| [`09_outputs_smart_record.md`](../../docs/architecture/deepstream/09_outputs_smart_record.md)           | Sinks, encoders, NvDsSR API                   |
| [`10_signal_vs_probe_deep_dive.md`](../../docs/architecture/deepstream/10_signal_vs_probe_deep_dive.md) | Signal vs pad probe — when to use which       |
| [`RAII.md`](../../docs/architecture/RAII.md)                                                            | GStreamer / CUDA resource management          |
| [`CMAKE.md`](../../docs/architecture/CMAKE.md)                                                          | Build system reference                        |

---

## Technology Version Detection

| Technology        | Version         | Config File                                                         |
| ----------------- | --------------- | ------------------------------------------------------------------- |
| C++               | **C++17**       | `CMakeLists.txt` — `set(CMAKE_CXX_STANDARD 17)`                     |
| CMake             | **3.16+**       | `CMakeLists.txt` — `cmake_minimum_required(VERSION 3.16)`           |
| NVIDIA DeepStream | **8.0**         | `Dockerfile` — `FROM nvcr.io/nvidia/deepstream:8.0-gc-triton-devel` |
| GStreamer         | **1.0 / 1.14+** | `pkg_check_modules(GST gstreamer-1.0)`                              |
| spdlog            | **1.14.1**      | Fetched via `FetchContent` in CMakeLists — `GIT_TAG v1.14.1`        |
| yaml-cpp          | **0.8.0**       | Fetched via `FetchContent` in CMakeLists — `GIT_TAG 0.8.0`          |
| hiredis           | **1.3.0**       | Fetched via `FetchContent` — Redis client                           |
| nlohmann/json     | **3.11.3**      | Fetched via `FetchContent` — JSON parsing                           |
| Build generator   | **Ninja**       | Preferred; fallback to Make                                         |

Do **not** use features beyond these versions.

---

## Strict Architecture Rules

### Layer Dependency Map

```
app/          → depends on: core, pipeline, domain, infrastructure, services
pipeline/     → depends on: core ONLY
domain/       → depends on: core ONLY
infrastructure/ → depends on: core ONLY
services/     → depends on: core ONLY
core/         → depends on: std library + GStreamer forward-declares ONLY
```

**Violations to avoid:**

- `#include "engine/pipeline/..."` inside `core/` → ❌ FORBIDDEN
- `#include "engine/infrastructure/..."` inside `pipeline/` → ❌ FORBIDDEN
- `#include "engine/domain/..."` inside `pipeline/` → ❌ FORBIDDEN
- Using `lantana::` namespace anywhere → ❌ WRONG (old project name)

### Interface-First Pattern

Every subsystem must have an interface in `core/` before any implementation:

```cpp
// ✅ CORRECT: Interface in core/
// core/include/engine/core/messaging/imessage_producer.hpp
namespace engine::core::messaging {
class IMessageProducer {
public:
    virtual ~IMessageProducer() = default;
    virtual bool connect() = 0;
    virtual bool publish(const std::string& topic, const std::string& payload) = 0;
};
} // namespace engine::core::messaging

// ✅ CORRECT: Implementation in infrastructure/
// infrastructure/messaging/include/engine/infrastructure/messaging/redis_stream_producer.hpp
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
std::vector<GstElement*> tails_;

// Constants / enum values — PascalCase for values
enum class PipelineState { Uninitialized, Ready, Playing, Paused, Stopped, Error };

// Files — snake_case
// pipeline_manager.hpp, source_builder.cpp, yaml_config_parser.hpp
```

## Doxygen Comment Requirements

When creating or refactoring C++ in this repository, use Doxygen comments as a hard rule for maintainability.

- Use `/** ... */` on all public interfaces (`I*`), public methods, and non-obvious structs.
- Start with `@brief`, then add `@param`, `@return`, and ownership/lifecycle notes when relevant.
- For constants/enums used across modules, add one-line Doxygen comments.
- Keep comments architecture-focused (contract, invariants, ownership), not line-by-line narration.

```cpp
/**
 * @brief Registers and activates event handlers defined in runtime configuration.
 * @param handlers_config Handler list parsed from YAML `event_handlers` block.
 * @return true if every enabled handler is validated and attached successfully.
 */
virtual bool register_event_handlers(
    std::vector<engine::core::config::CustomHandlerConfig>& handlers_config) = 0;
```

```cpp
/** @brief End-of-stream event key emitted from GstBus EOS messages. */
inline constexpr std::string_view ON_EOS = "on_eos";
```

---

## Config Types — Core Patterns

All config structs live in `core/include/engine/core/config/`. Builders receive `const engine::core::config::PipelineConfig&` — the full config, not slices.

```cpp
// PipelineConfig is the single root config
struct PipelineConfig {
    std::string         version;
    PipelineMetaConfig  pipeline;       // id, name, log_level, gst_log_level, dot_file_dir, log_file
    QueueConfig         queue_defaults; // default for every queue: {}

    SourcesConfig       sources;        // nvmultiurisrcbin + cameras[] + smart_record
    ProcessingConfig    processing;     // elements[] (nvinfer, nvtracker, ...)
    VisualsConfig       visuals;        // enable + elements[] (tiler, osd)
    std::vector<OutputConfig>       outputs;        // each has id, type, elements[]
    std::vector<EventHandlerConfig> event_handlers; // pad probes (smart_record, crop_objects, ...)
};

// Element builders access via:
// config.sources                         — SourcesConfig (single block)
// config.processing.elements[index]      — ProcessingElementConfig
// config.visuals.elements[index]         — VisualsElementConfig
// config.outputs[i].elements[j]          — OutputElementConfig
// config.event_handlers[i]               — EventHandlerConfig
```

**No `std::variant` over backend types** — vms-engine is DeepStream-native. Do not use patterns from lantanav2 like `ProcessingBackendOptions` or `SourceBackendOptions`.

---

## Element Builder Pattern

```cpp
// ✅ CORRECT element builder pattern
// pipeline/include/engine/pipeline/builders/my_element_builder.hpp
#pragma once
#include "engine/core/builders/ielement_builder.hpp"
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>

namespace engine::pipeline::builders {

class MyElementBuilder : public engine::core::builders::IElementBuilder {
public:
    GstElement* build(const engine::core::config::PipelineConfig& config,
                      int index = 0) override;
};

} // namespace engine::pipeline::builders
```

```cpp
// ✅ CORRECT implementation — property setting pattern
#include "engine/pipeline/builders/my_element_builder.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::builders {

GstElement* MyElementBuilder::build(
    const engine::core::config::PipelineConfig& config, int index)
{
    const auto& elem_cfg = config.processing.elements[index]; // access own section

    GstElement* element = gst_element_factory_make("nvmyelement", elem_cfg.id.c_str());
    if (!element) {
        LOG_E("Failed to create nvmyelement '{}'", elem_cfg.id);
        return nullptr;
    }

    // GStreamer properties — use g_object_set with C strings
    g_object_set(G_OBJECT(element),
                 "gpu-id",     static_cast<gint>(elem_cfg.gpu_id),
                 "batch-size", static_cast<gint>(elem_cfg.batch_size),
                 nullptr);  // ALWAYS terminate g_object_set with nullptr

    LOG_I("Built element '{}' (type: nvmyelement)", elem_cfg.id);
    return element;
}

} // namespace engine::pipeline::builders
```

---

## GStreamer API Patterns

```cpp
// ✅ Always terminate g_object_set with nullptr
g_object_set(G_OBJECT(element),
             "property-name", value,
             nullptr);

// ✅ Use RAII for GstElement ownership in builders
// Builders return raw GstElement* — ownership transferred to GstBin
// PipelineBuilder adds to bin and manages lifecycle

// ✅ Dynamic pad connections (nvmultiurisrcbin → muxer)
g_signal_connect(source_bin, "pad-added",
                 G_CALLBACK(on_pad_added_callback), muxer);

// ✅ Probe pattern
static GstPadProbeReturn probe_callback(GstPad* pad,
                                         GstPadProbeInfo* info,
                                         gpointer user_data) {
    GstBuffer* buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buffer);
    // ... process metadata
    return GST_PAD_PROBE_OK;
}

gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER,
                  probe_callback, user_data, nullptr);
```

---

## YAML Config Conventions

YAML property names use `snake_case`. The parser maps them to GStreamer's `kebab-case`:

```yaml
# ✅ YAML uses snake_case — matches new deepstream_default.yml
queue_defaults:
  max_size_buffers: 10
  leaky: 2 # 0=none, 1=upstream, 2=downstream

sources:
  type: nvmultiurisrcbin
  # NOTE: ip_address and port are NOT configured — DS8 ip-address setter causes SIGSEGV.
  # REST API stays disabled; element defaults to 0.0.0.0 internally.
  max_batch_size: 4
  mode: 0 # 0=video-only  1=audio-only
  gpu_id: 0
  width: 1920
  height: 1080
  cameras:
    - id: camera-01
      uri: "rtsp://host/stream"
  smart_record: 2 # int enum, NOT string
  smart_rec_dir_path: "/opt/engine/data/rec"

processing:
  elements:
    - id: pgie_detection
      type: nvinfer
      role: primary_inference
      config_file: "/opt/engine/data/components/pgie/config.yml"
      process_mode: 1 # int: 1=primary, 2=secondary
      batch_size: 4
      queue: {} # explicit queue inline
    - id: tracker
      type: nvtracker
      ll_lib_file: "/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so"
      ll_config_file: "/opt/engine/data/config/tracker_NvDCF_perf.yml"
      tracker_width: 640
      tracker_height: 640
      compute_hw: 1 # int: 0=default, 1=GPU, 2=VIC
      queue: {}

visuals:
  enable: true
  elements:
    - id: tiler
      type: nvmultistreamtiler
      rows: 2
      columns: 2
      queue: {}
    - id: osd
      type: nvdsosd
      process_mode: 1
      display_bbox: true
      queue: {}

outputs:
  - id: rtsp_out
    type: rtsp_client
    elements:
      - id: encoder
        type: nvv4l2h264enc
        bitrate: 3000000
      - id: sink
        type: rtspclientsink
        location: rtsp://host:8554/stream
        queue: {}

event_handlers:
  - id: smart_record
    enable: true
    type: on_detect
    probe_element: tracker
    trigger: smart_record
    label_filter: [car, person, truck]
```

- All `enum` values → **integers** in YAML (never strings)
- GStreamer docs use hyphens (`ll-lib-file`) → YAML uses underscores (`ll_lib_file`)
- Reference: `docs/configs/deepstream_default.yml`

---

## DeepStream Plugin Properties — g_object_set Reference

When calling `g_object_set`, use the GStreamer **kebab-case** property name as a C string, with the correct GLib type. Always end with `nullptr`.

### nvmultiurisrcbin

> ⚠️ **DS8 SIGSEGV**: `ip-address` and `port` are **NOT set** — setting `ip-address` via `g_object_set` crashes DeepStream 8.0. Omit them entirely; the element defaults to `0.0.0.0` with REST API disabled.

```cpp
// Group 1 — nvmultiurisrcbin direct (ip-address/port intentionally omitted — DS8 SIGSEGV)
g_object_set(G_OBJECT(src_bin),
    "max-batch-size",           (gint)  cfg.max_batch_size,
    "mode",                     (gint)  cfg.mode,                 // 0=video, 1=audio
    nullptr);

// Group 2 — nvurisrcbin per-source passthrough
g_object_set(G_OBJECT(src_bin),
    "gpu-id",                   (gint)  cfg.gpu_id,
    "num-extra-surfaces",       (gint)  cfg.num_extra_surfaces,
    "cudadec-memtype",          (gint)  cfg.cudadec_memtype,      // 0=device,1=pinned,2=unified
    "select-rtp-protocol",      (gint)  cfg.select_rtp_protocol,  // 4=TCP-only
    "rtsp-reconnect-interval",  (gint)  cfg.rtsp_reconnect_interval,
    "rtsp-reconnect-attempts",  (gint)  cfg.rtsp_reconnect_attempts,
    "latency",                  (guint) cfg.latency,              // ms
    "udp-buffer-size",          (guint) cfg.udp_buffer_size,
    "drop-pipeline-eos",        (gboolean) cfg.drop_pipeline_eos,
    nullptr);

// Group 3 — nvstreammux passthrough
g_object_set(G_OBJECT(src_bin),
    "width",                    (gint)  cfg.width,
    "height",                   (gint)  cfg.height,
    "batched-push-timeout",     (gint)  cfg.batched_push_timeout, // µs
    "live-source",              (gboolean) cfg.live_source,
    nullptr);

// Smart Record (only if enabled)
if (cfg.smart_record > 0) {
    g_object_set(G_OBJECT(src_bin),
        "smart-record",             (gint)  cfg.smart_record,         // 0/1/2
        "smart-rec-dir-path",       (const gchar*) cfg.smart_rec_dir_path.c_str(),
        "smart-rec-file-prefix",    (const gchar*) cfg.smart_rec_file_prefix.c_str(),
        "smart-rec-cache",          (gint)  cfg.smart_rec_cache,
        "smart-rec-default-duration",(gint) cfg.smart_rec_default_duration,
        nullptr);
}
```

### nvinfer (PGIE)

```cpp
g_object_set(G_OBJECT(infer),
    "config-file-path",   (const gchar*) elem_cfg.config_file.c_str(),
    "process-mode",       (gint)  elem_cfg.process_mode,    // 1=primary, 2=secondary
    "batch-size",         (gint)  elem_cfg.batch_size,
    "interval",           (gint)  elem_cfg.interval,        // 0=every batch
    "gpu-id",             (gint)  elem_cfg.gpu_id,
    "gie-unique-id",      (gint)  elem_cfg.unique_id,
    nullptr);

// SGIE only — add these:
g_object_set(G_OBJECT(sgie),
    "operate-on-gie-id",    (gint)  elem_cfg.operate_on_gie_id,
    "operate-on-class-ids", (const gchar*) elem_cfg.operate_on_class_ids.c_str(), // "0:2"
    nullptr);
```

### nvtracker

```cpp
g_object_set(G_OBJECT(tracker),
    "ll-lib-file",      (const gchar*) tracker_cfg.ll_lib_file.c_str(),
    "ll-config-file",   (const gchar*) tracker_cfg.ll_config_file.c_str(),
    "tracker-width",    (gint) tracker_cfg.tracker_width,
    "tracker-height",   (gint) tracker_cfg.tracker_height,
    "gpu-id",           (gint) tracker_cfg.gpu_id,
    "compute-hw",       (gint) tracker_cfg.compute_hw,  // 0=default, 1=GPU, 2=VIC
    "display-tracking-id", (gboolean) tracker_cfg.display_tracking_id,
    nullptr);
```

### nvdsosd

```cpp
g_object_set(G_OBJECT(osd),
    "process-mode",   (gint)     osd_cfg.process_mode,  // 0=CPU, 1=GPU, 2=HW
    "display-bbox",   (gboolean) osd_cfg.display_bbox,
    "display-text",   (gboolean) osd_cfg.display_text,
    "display-mask",   (gboolean) osd_cfg.display_mask,
    "border-width",   (gint)     osd_cfg.border_width,
    "gpu-id",         (gint)     osd_cfg.gpu_id,
    nullptr);
```

### nvmultistreamtiler

```cpp
g_object_set(G_OBJECT(tiler),
    "rows",     (guint) tiler_cfg.rows,
    "columns",  (guint) tiler_cfg.columns,
    "width",    (guint) tiler_cfg.width,
    "height",   (guint) tiler_cfg.height,
    "gpu-id",   (gint)  tiler_cfg.gpu_id,
    nullptr);
```

### nvdsanalytics

```cpp
g_object_set(G_OBJECT(analytics),
    "config-file",            (const gchar*) analytics_cfg.config_file.c_str(),
    "gpu-id",                 (gint)     analytics_cfg.gpu_id,
    "enable-secondary-input", (gboolean) analytics_cfg.enable_secondary_input,
    nullptr);
```

### nvmsgconv + nvmsgbroker

```cpp
// nvmsgconv
g_object_set(G_OBJECT(msgconv),
    "config",        (const gchar*) mq_cfg.msgconv_config.c_str(),
    "payload-type",  (gint) mq_cfg.payload_type,  // 0=DEEPSTREAM, 1=MINIMAL
    "comp-id",       (gint) mq_cfg.comp_id,
    nullptr);

// nvmsgbroker
g_object_set(G_OBJECT(broker),
    "proto-lib",  (const gchar*) mq_cfg.proto_lib.c_str(),
    "conn-str",   (const gchar*) mq_cfg.conn_str.c_str(),
    "topic",      (const gchar*) mq_cfg.topic.c_str(),
    "config",     (const gchar*) mq_cfg.broker_config.c_str(),
    "sync",       (gboolean) FALSE,
    nullptr);
// proto-lib paths: /opt/nvidia/deepstream/deepstream/lib/libnvds_kafka_proto.so
//                  /opt/nvidia/deepstream/deepstream/lib/libnvds_redis_proto.so
```

### nvv4l2h264enc / nvv4l2h265enc

```cpp
g_object_set(G_OBJECT(encoder),
    "bitrate",          (guint)    enc_cfg.bitrate,          // bps, e.g. 4000000
    "iframeinterval",   (guint)    enc_cfg.iframeinterval,
    "preset-level",     (gint)     enc_cfg.preset_level,     // 1=UltraFast...4=Medium
    "insert-sps-pps",   (gboolean) TRUE,                     // REQUIRED for RTSP
    "maxperf-enable",   (gboolean) enc_cfg.maxperf_enable,
    nullptr);
```

### NvDs Metadata access in pad probes

```cpp
// Attach probe to a sink pad
GstPad* pad = gst_element_get_static_pad(element, "sink");
gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, osd_probe_callback, nullptr, nullptr);
gst_object_unref(pad);

// Inside probe callback
static GstPadProbeReturn osd_probe_callback(GstPad*, GstPadProbeInfo* info, gpointer) {
    GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
    NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buf);

    for (NvDsMetaList* l_frame = batch_meta->frame_meta_list; l_frame; l_frame = l_frame->next) {
        NvDsFrameMeta* frame_meta = (NvDsFrameMeta*)(l_frame->data);

        for (NvDsMetaList* l_obj = frame_meta->obj_meta_list; l_obj; l_obj = l_obj->next) {
            NvDsObjectMeta* obj = (NvDsObjectMeta*)(l_obj->data);
            // obj->class_id, obj->confidence, obj->object_id (tracker id)
            // obj->rect_params.left/top/width/height
        }
    }
    return GST_PAD_PROBE_OK;
}
```

---

## Memory Management & RAII

> **Full RAII guide → [`docs/architecture/RAII.md`](../../docs/architecture/RAII.md)**  
> Covers: heap, file handles, sockets, mutex/locks, timers, scope guards,
> GStreamer resources, NvDs rules, custom destructor classes, Rule of Five, GPU/CUDA resources, anti-patterns.

### Ownership Rules (quick ref)

```
gst_element_factory_make()    → caller owns; call release() after gst_bin_add()
gst_element_get_static_pad()  → gst_object_unref() when done (even read-only)
gst_caps_new_*()              → gst_caps_unref() when done
gst_pipeline_get_bus()        → gst_object_unref() when done
g_main_loop_new()             → g_main_loop_unref() when done
g_object_get(...gchar*...)    → g_free() the string
NvDsBatchMeta / Frame / Object → DO NOT FREE — pipeline owns
```

### RAII Type Aliases (`gst_utils.hpp`)

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

### Builder Pattern (most common usage)

```cpp
// ✅ CORRECT — guard auto-unrefs on all failure paths
auto elem = engine::core::utils::make_gst_element("nvinfer", id.c_str());
if (!elem) { LOG_E("Failed"); return nullptr; }  // auto-unref
g_object_set(G_OBJECT(elem.get()), "config-file-path", cfg.c_str(), nullptr);
if (!gst_bin_add(GST_BIN(bin_), elem.get())) return nullptr;  // auto-unref
return elem.release();  // bin owns — disarm guard

// ❌ WRONG — leak on early return
GstElement* e = gst_element_factory_make("nvinfer", id.c_str());
if (!condition) return nullptr;   // leak: e never unref'd
```

**Multi-step cleanup** (remove_watch → unref; set_state(NULL) → unref):  
Write a custom class with `~Destructor()`. See `GstBusGuard` and `GstPipelineOwner` in [`RAII.md`](../../docs/architecture/RAII.md#9-custom-raii-class-with-destructor).

**Beyond GStreamer** (heap, file handles, sockets, mutex/locks, timers, scope guards):  
See [`RAII.md`](../../docs/architecture/RAII.md).

---

## Logging

Always use `LOG_*` macros. Never `std::cout`, `printf`, or raw `spdlog::` calls in library code.

```cpp
#include "engine/core/utils/logger.hpp"

LOG_T("Trace: {}", detail);          // Verbose debug
LOG_D("Debug: element '{}' ready", id);
LOG_I("Info: Pipeline started, {} sources", count);
LOG_W("Warning: field '{}' deprecated", name);
LOG_E("Error: Failed to link {} → {}", src_name, sink_name);
LOG_C("Critical: GStreamer init failed");
```

---

## CMakeLists.txt Patterns

```cmake
# New library target pattern (follow existing CMakeLists):
add_library(vms_engine_mylayer STATIC
    src/my_file.cpp
)

target_include_directories(vms_engine_mylayer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${DEEPSTREAM_INCLUDE_DIRS}
        ${GSTREAMER_INCLUDE_DIRS}
)

target_link_libraries(vms_engine_mylayer
    PUBLIC
        vms_engine_core           # Layer dependency → core only
    PRIVATE
        ${GSTREAMER_LIBRARIES}
)

set_target_properties(vms_engine_mylayer PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
```

---

## Common Anti-Patterns to Avoid

```cpp
// ❌ Wrong namespace (old project)
namespace lantana::core::pipeline { }

// ❌ Include wrong project's headers
#include "lantana/core/builders/ibuilder_factory.hpp"

// ❌ pipeline/ including infrastructure/
#include "engine/infrastructure/messaging/redis_stream_producer.hpp"  // inside pipeline/

// ❌ Missing nullptr terminator in g_object_set
g_object_set(G_OBJECT(el), "gpu-id", 0);  // BUG: missing nullptr

// ❌ Returning config slices from builders
GstElement* build(const InferenceConfig& slice); // WRONG — pass full PipelineConfig

// ❌ std::variant for backend selection (lantanav2 pattern, removed in vms-engine)
std::variant<DeepStreamOptions, DLStreamerOptions> backend_options;

// ❌ std::cout in library code
std::cout << "Building element: " << name << std::endl;  // use LOG_I instead

// ❌ Global state
static GstPipeline* g_pipeline = nullptr;  // avoid; use class members

// ❌ C++20 features
auto result = std::ranges::find(vec, val);  // ranges = C++20, NOT allowed
std::format("hello {}", name);              // format = C++20, use fmt::format instead
```

---

## Infrastructure Adapter Pattern

```cpp
// ✅ YAML parser section adds to PipelineConfig
// infrastructure/config_parser/yaml_parser_processing.cpp
namespace engine::infrastructure::config_parser {

void YamlConfigParser::parse_processing(
    const YAML::Node& node,
    engine::core::config::ProcessingConfig& out)
{
    if (!node || !node.IsMap()) return;

    if (node["elements"] && node["elements"].IsSequence()) {
        for (const auto& elem_node : node["elements"]) {
            engine::core::config::ProcessingElementConfig elem;
            elem.id   = elem_node["id"].as<std::string>("");
            elem.type = elem_node["type"].as<std::string>("");
            // ... parse properties with snake_case keys
            out.elements.push_back(std::move(elem));
        }
    }
}

} // namespace engine::infrastructure::config_parser
```

---

## File Header Template

```cpp
// pipeline/include/engine/pipeline/builders/my_builder.hpp
#pragma once

#include "engine/core/builders/ielement_builder.hpp"
#include "engine/core/config/config_types.hpp"

#include <gst/gst.h>

namespace engine::pipeline::builders {

/**
 * @brief Builds <element name> GStreamer element from pipeline config.
 *
 * Implements IElementBuilder for <gst-element-name> element.
 * Relevant config section: config.processing.elements[index]
 */
class MyBuilder : public engine::core::builders::IElementBuilder {
public:
    GstElement* build(const engine::core::config::PipelineConfig& config,
                      int index = 0) override;
};

} // namespace engine::pipeline::builders
```

---

## Include Path Conventions

```cpp
// Project headers — angle brackets with full path from include root
#include "engine/core/pipeline/ipipeline_manager.hpp"
#include "engine/core/config/config_types.hpp"
#include "engine/core/utils/logger.hpp"
#include "engine/pipeline/pipeline_manager.hpp"

// System / external headers — angle brackets
#include <gst/gst.h>
#include <nvds_meta.h>
#include <glib.h>
#include <string>
#include <vector>
#include <memory>
```

Order: project headers first → system/external headers.
