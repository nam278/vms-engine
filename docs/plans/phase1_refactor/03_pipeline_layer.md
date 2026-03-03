# Plan 03 — Pipeline Layer (Builders, Manager, Probes, Event Handlers)

> Create all files in `pipeline/` from scratch.
> This layer implements the core interfaces defined in Plan 02 using GStreamer/DeepStream SDK.
> No migration from lantanav2 — all files are new creations.

---

## Prerequisites

- Plan 02 completed (`vms_engine_core` compiles — all `I*` interfaces and config types defined)
- Specifically: `IElementBuilder`, `IPipelineBuilder`, `IBuilderFactory`, `IPipelineManager`, `IEventHandler`, `IHandlerManager`
- Config types: `PipelineConfig`, `SourcesConfig`, `ProcessingConfig`, `VisualsConfig`, `OutputConfig`, `EventHandlerConfig`, `QueueConfig`

---

## Architecture Overview

```
pipeline/
  include/engine/pipeline/
    pipeline_manager.hpp            # IPipelineManager implementation
    runtime_stream_manager.hpp      # Runtime add/remove streams
    smart_record_controller.hpp     # NvDsSR API wrapper
    builder_factory.hpp             # IBuilderFactory implementation
    config_validator.hpp            # Pipeline config validation
    block_builders/
      source_block_builder.hpp      # Phase 1: source bin (nvmultiurisrcbin)
      processing_block_builder.hpp  # Phase 2: inference chain
      visuals_block_builder.hpp     # Phase 3: tiler + OSD
      outputs_block_builder.hpp     # Phase 4: encoders + sinks
    builders/
      source_builder.hpp            # nvmultiurisrcbin builder
      muxer_builder.hpp             # nvstreammux (fallback/explicit)
      queue_builder.hpp             # GStreamer queue element
      infer_builder.hpp             # nvinfer (PGIE + SGIE)
      tracker_builder.hpp           # nvtracker
      analytics_builder.hpp         # nvdsanalytics
      tiler_builder.hpp             # nvmultistreamtiler
      osd_builder.hpp               # nvdsosd
      encoder_builder.hpp           # nvv4l2h264enc / nvv4l2h265enc
      sink_builder.hpp              # rtspclientsink / fakesink / filesink
      msgconv_builder.hpp           # nvmsgconv
      msgbroker_builder.hpp         # nvmsgbroker
      demuxer_builder.hpp           # nvstreamdemux
    event_handlers/
      handler_manager.hpp           # IHandlerManager implementation
      smart_record_handler.hpp      # smart record event handler
      crop_detected_obj_handler.hpp # crop object on detection
      ext_proc_handler.hpp          # external processor handler
      object_crop.hpp               # crop utility (header-only)
    linking/
      pipeline_linker.hpp           # static/dynamic element linking
    probes/
      probe_handler_manager.hpp     # pad probe registration
      smart_record_probe_handler.hpp
      crop_object_handler.hpp       # pad probe for crop
      class_id_namespace_handler.hpp
  src/  (mirrors include structure)
```

---

## Full Config Pattern — ALL Builders

Every element builder receives `const engine::core::config::PipelineConfig& config` (full config).
The `index` parameter selects the element within the relevant config list.

```cpp
// ✅ ALL builders follow this exact signature
GstElement* SomeBuilder::build(
    const engine::core::config::PipelineConfig& config,
    int index = 0) override;
```

### Config Section Access Map

| Builder               | Config access                                    | Type                     |
| --------------------- | ------------------------------------------------ | ------------------------ |
| `SourceBuilder`       | `config.sources`                                 | `SourcesConfig`          |
| `MuxerBuilder`        | `config.sources` (batch_size, width, height)     | `SourcesConfig`          |
| `QueueBuilder`        | `QueueConfig` directly (not PipelineConfig)      | `QueueConfig`            |
| `InferBuilder`        | `config.processing.elements[index]`              | `ProcessingElementConfig`|
| `TrackerBuilder`      | `config.processing.elements[index]`              | `ProcessingElementConfig`|
| `AnalyticsBuilder`    | `config.processing.elements[index]`              | `ProcessingElementConfig`|
| `TilerBuilder`        | `config.visuals.elements[index]`                 | `VisualsElementConfig`   |
| `OsdBuilder`          | `config.visuals.elements[index]`                 | `VisualsElementConfig`   |
| `EncoderBuilder`      | `config.outputs[outputIdx].elements[elemIdx]`    | `OutputElementConfig`    |
| `SinkBuilder`         | `config.outputs[outputIdx].elements[elemIdx]`    | `OutputElementConfig`    |
| `MsgconvBuilder`      | `config.outputs[outputIdx].elements[elemIdx]`    | `OutputElementConfig`    |
| `MsgbrokerBuilder`    | `config.outputs[outputIdx].elements[elemIdx]`    | `OutputElementConfig`    |
| `DemuxerBuilder`      | `config.sources` (num sources for pads)          | `SourcesConfig`          |

**QueueBuilder exception**: Takes `QueueConfig` (not `PipelineConfig`) since queues are inline elements
with their own config resolved by the block builders before calling `QueueBuilder`.

---

## Section 3.1 — Root-Level Manager Classes

### Files to Create

| File (header)                                   | File (source)                                   | Class                    |
| ----------------------------------------------- | ----------------------------------------------- | ------------------------ |
| `include/engine/pipeline/pipeline_manager.hpp`  | `src/pipeline_manager.cpp`                      | `PipelineManager`        |
| `include/engine/pipeline/runtime_stream_manager.hpp` | `src/runtime_stream_manager.cpp`           | `RuntimeStreamManager`   |
| `include/engine/pipeline/smart_record_controller.hpp` | `src/smart_record_controller.cpp`         | `SmartRecordController`  |
| `include/engine/pipeline/builder_factory.hpp`   | `src/builder_factory.cpp`                       | `BuilderFactory`         |
| `include/engine/pipeline/config_validator.hpp`  | `src/config_validator.cpp`                      | `ConfigValidator`        |

### `PipelineManager` — Key Notes

- Implements `engine::core::pipeline::IPipelineManager`
- Owns `GstElement* pipeline_` as a `GstPipelineOwner` RAII wrapper (see RAII.md)
- Sets up `GstBus` watch — `gst_bus_add_watch(bus, on_bus_message, this)`
- State machine: `Uninitialized → Ready → Playing → Paused → Stopped → Error`
- Does NOT include any DeepStream SDK headers — only GStreamer `<gst/gst.h>`
- Delegates pipeline construction to `IPipelineBuilder` (injected via constructor)

```cpp
// pipeline/include/engine/pipeline/pipeline_manager.hpp
#pragma once
#include "engine/core/pipeline/ipipeline_manager.hpp"
#include "engine/core/builders/ipipeline_builder.hpp"
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>
#include <memory>

namespace engine::pipeline {

class PipelineManager : public engine::core::pipeline::IPipelineManager {
public:
    explicit PipelineManager(
        std::unique_ptr<engine::core::builders::IPipelineBuilder> builder);

    bool initialize(engine::core::config::PipelineConfig& config) override;
    bool start() override;
    bool stop() override;
    bool pause() override;
    engine::core::pipeline::PipelineState get_state() const override;

    ~PipelineManager() override;

private:
    std::unique_ptr<engine::core::builders::IPipelineBuilder> builder_;
    GstElement* pipeline_ = nullptr;
    GMainLoop*  loop_     = nullptr;
    engine::core::pipeline::PipelineState state_{
        engine::core::pipeline::PipelineState::Uninitialized};

    static gboolean on_bus_message(GstBus* bus, GstMessage* msg, gpointer data);
    void handle_eos();
    void handle_error(GError* err, const gchar* debug);
};

} // namespace engine::pipeline
```

---

## Section 3.2 — Block Builders (Pipeline Build Phases)

Block builders orchestrate groups of GStreamer elements for each pipeline phase.
Each block builder is called once by `IPipelineBuilder::build()`.

### Files to Create

| File (header)                                                | File (source)                                          |
| ------------------------------------------------------------ | ------------------------------------------------------ |
| `include/engine/pipeline/block_builders/source_block_builder.hpp`      | `src/block_builders/source_block_builder.cpp`     |
| `include/engine/pipeline/block_builders/processing_block_builder.hpp`  | `src/block_builders/processing_block_builder.cpp` |
| `include/engine/pipeline/block_builders/visuals_block_builder.hpp`     | `src/block_builders/visuals_block_builder.cpp`    |
| `include/engine/pipeline/block_builders/outputs_block_builder.hpp`     | `src/block_builders/outputs_block_builder.cpp`    |

### Build Phase Overview

```
Phase 1 — SourceBlockBuilder:
    nvmultiurisrcbin → [output_queue]
    tail: src_tail (GstElement*)

Phase 2 — ProcessingBlockBuilder:
    [input from src_tail]
    → [queue?] → nvinfer(PGIE) → [queue?] → nvtracker → [queue?] → nvinfer(SGIE)* → [queue?]
    → nvdsanalytics?
    → [output_queue]
    tail: proc_tail

Phase 3 — VisualsBlockBuilder:
    [input from proc_tail]
    → [queue?] → nvmultistreamtiler → [queue?] → nvdsosd → [queue?]
    → [output_queue]
    tail: vis_tail

Phase 4 — OutputsBlockBuilder:
    For each output in config.outputs:
      [input from tee or proc_tail/vis_tail]
      → [queue?] → nvv4l2h264enc/nvv4l2h265enc → [queue?] → sink
      OR
      → [queue?] → nvmsgconv → nvmsgbroker
    Connects: demuxer for multi-sink outputs
```

### `tails_` Map Pattern

Block builders communicate which element is the "tail" (last element before next block)
via the `tails_` map. This is the standard linkage contract.

```cpp
// Each block builder updates this map after building its phase
// key = descriptive name, value = GstElement* (tail of this phase's chain)
std::unordered_map<std::string, GstElement*> tails_;

// After Phase 1:  tails_["src"]  = last element of source block
// After Phase 2:  tails_["proc"] = last element of processing block
// After Phase 3:  tails_["vis"]  = last element of visuals block
// Phase 4 reads tails_["proc"] or tails_["vis"] to connect sinks
```

---

## Section 3.3 — Element Builders

Each element builder creates exactly **one** GstElement and configures its properties.
All use the RAII `make_gst_element()` guard pattern.

### Files to Create

| Header                                                 | Source                                          | GStreamer element       |
| ------------------------------------------------------ | ----------------------------------------------- | ----------------------- |
| `include/engine/pipeline/builders/source_builder.hpp`  | `src/builders/source_builder.cpp`               | `nvmultiurisrcbin`      |
| `include/engine/pipeline/builders/muxer_builder.hpp`   | `src/builders/muxer_builder.cpp`                | `nvstreammux`           |
| `include/engine/pipeline/builders/queue_builder.hpp`   | `src/builders/queue_builder.cpp`                | `queue`                 |
| `include/engine/pipeline/builders/infer_builder.hpp`   | `src/builders/infer_builder.cpp`                | `nvinfer`               |
| `include/engine/pipeline/builders/tracker_builder.hpp` | `src/builders/tracker_builder.cpp`              | `nvtracker`             |
| `include/engine/pipeline/builders/analytics_builder.hpp` | `src/builders/analytics_builder.cpp`          | `nvdsanalytics`         |
| `include/engine/pipeline/builders/tiler_builder.hpp`   | `src/builders/tiler_builder.cpp`                | `nvmultistreamtiler`    |
| `include/engine/pipeline/builders/osd_builder.hpp`     | `src/builders/osd_builder.cpp`                  | `nvdsosd`               |
| `include/engine/pipeline/builders/encoder_builder.hpp` | `src/builders/encoder_builder.cpp`              | `nvv4l2h264enc/265enc`  |
| `include/engine/pipeline/builders/sink_builder.hpp`    | `src/builders/sink_builder.cpp`                 | `rtspclientsink` / `fakesink` / `filesink` |
| `include/engine/pipeline/builders/msgconv_builder.hpp` | `src/builders/msgconv_builder.cpp`              | `nvmsgconv`             |
| `include/engine/pipeline/builders/msgbroker_builder.hpp` | `src/builders/msgbroker_builder.cpp`          | `nvmsgbroker`           |
| `include/engine/pipeline/builders/demuxer_builder.hpp` | `src/builders/demuxer_builder.cpp`              | `nvstreamdemux`         |

### Standard Builder Template

```cpp
// pipeline/include/engine/pipeline/builders/infer_builder.hpp
#pragma once
#include "engine/core/builders/ielement_builder.hpp"
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>

namespace engine::pipeline::builders {

/**
 * @brief Builds nvinfer GStreamer element (PGIE or SGIE) from pipeline config.
 *
 * Reads config from config.processing.elements[index].
 * Supports role: primary_inference (process_mode=1) and secondary_inference (process_mode=2).
 */
class InferBuilder : public engine::core::builders::IElementBuilder {
public:
    /**
     * @param bin  GstBin to add the created element into (PipelineBuilder owns the bin)
     */
    explicit InferBuilder(GstElement* bin);

    /**
     * @brief Create and configure nvinfer element.
     * @param config Full pipeline config.
     * @param index  Index into config.processing.elements[].
     * @return Configured GstElement* (bin owns it), or nullptr on failure.
     */
    GstElement* build(const engine::core::config::PipelineConfig& config,
                      int index = 0) override;

private:
    GstElement* bin_ = nullptr;
};

} // namespace engine::pipeline::builders
```

```cpp
// pipeline/src/builders/infer_builder.cpp
#include "engine/pipeline/builders/infer_builder.hpp"
#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::builders {

InferBuilder::InferBuilder(GstElement* bin) : bin_(bin) {}

GstElement* InferBuilder::build(
    const engine::core::config::PipelineConfig& config, int index)
{
    const auto& elem_cfg = config.processing.elements[index];
    const auto& id       = elem_cfg.id;

    // ✅ RAII: auto-unref on any return before gst_bin_add()
    auto elem = engine::core::utils::make_gst_element("nvinfer", id.c_str());
    if (!elem) {
        LOG_E("Failed to create nvinfer '{}'", id);
        return nullptr;
    }

    g_object_set(G_OBJECT(elem.get()),
        "config-file-path", elem_cfg.config_file.c_str(),
        "process-mode",     static_cast<gint>(elem_cfg.process_mode),
        "batch-size",       static_cast<gint>(elem_cfg.batch_size),
        "gie-unique-id",    static_cast<gint>(elem_cfg.unique_id),
        "gpu-id",           static_cast<gint>(elem_cfg.gpu_id),
        nullptr);

    // SGIE-only properties
    if (elem_cfg.process_mode == 2) {
        g_object_set(G_OBJECT(elem.get()),
            "operate-on-gie-id",    static_cast<gint>(elem_cfg.operate_on_gie_id),
            nullptr);
    }

    if (!gst_bin_add(GST_BIN(bin_), elem.get())) {
        LOG_E("Failed to add nvinfer '{}' to bin", id);
        return nullptr;  // RAII guard cleans up
    }

    LOG_I("Built nvinfer '{}' (mode={})", id, elem_cfg.process_mode);
    return elem.release();  // bin owns — disarm guard
}

} // namespace engine::pipeline::builders
```

### QueueBuilder — Takes `QueueConfig` Directly

```cpp
// pipeline/include/engine/pipeline/builders/queue_builder.hpp
#pragma once
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>

namespace engine::pipeline::builders {

/**
 * @brief Builds GStreamer queue element with QueueConfig settings.
 *
 * Called by BlockBuilders when a queue: {} entry appears in the config element list.
 * Takes QueueConfig directly (already resolved by BlockBuilder from config).
 */
class QueueBuilder {
public:
    explicit QueueBuilder(GstElement* bin);

    /**
     * @param cfg   Resolved queue config (max_size_buffers, leaky, etc.)
     * @param name  Element name (unique within the pipeline)
     * @return Configured GstElement* (bin owns it), or nullptr on failure.
     */
    GstElement* build(const engine::core::config::QueueConfig& cfg,
                      const std::string& name);

private:
    GstElement* bin_ = nullptr;
};

} // namespace engine::pipeline::builders
```

---

## Section 3.4 — Event Handlers

Event handlers fire in response to detection events from pad probes and GSignals.
Each handler receives `IMessageProducer*` via constructor (no static coupling).

### Files to Create

| Header                                                           | Source                                               |
| ---------------------------------------------------------------- | ---------------------------------------------------- |
| `include/engine/pipeline/event_handlers/handler_manager.hpp`    | `src/event_handlers/handler_manager.cpp`             |
| `include/engine/pipeline/event_handlers/smart_record_handler.hpp` | `src/event_handlers/smart_record_handler.cpp`      |
| `include/engine/pipeline/event_handlers/crop_detected_obj_handler.hpp` | `src/event_handlers/crop_detected_obj_handler.cpp` |
| `include/engine/pipeline/event_handlers/ext_proc_handler.hpp`   | `src/event_handlers/ext_proc_handler.cpp`            |
| `include/engine/pipeline/event_handlers/object_crop.hpp`        | *(header-only utility)*                              |

### Handler Dependency Injection Pattern

```cpp
// ✅ CORRECT: IMessageProducer* injected, no static coupling
class SmartRecordHandler : public engine::core::handlers::IEventHandler {
public:
    SmartRecordHandler(
        engine::core::messaging::IMessageProducer* producer,
        engine::core::storage::IStorageManager* storage);

    bool handle(const engine::core::handlers::HandlerContext& ctx) override;

private:
    engine::core::messaging::IMessageProducer* producer_{nullptr};
    engine::core::storage::IStorageManager*    storage_{nullptr};
};
```

```cpp
// ✅ CORRECT: Pad guard for probe registration
void SmartRecordHandler::register_probe(GstElement* element) {
    engine::core::utils::GstPadPtr pad(
        gst_element_get_static_pad(element, "sink"), gst_object_unref);
    if (!pad) { LOG_E("Pad not found"); return; }
    gst_pad_add_probe(pad.get(), GST_PAD_PROBE_TYPE_BUFFER,
                      on_probe_buffer, this, nullptr);
}  // gst_object_unref(pad) called automatically
```

---

## Section 3.5 — Linking

### Files to Create

| Header                                                  | Source                                     |
| -------------------------------------------------------- | ------------------------------------------ |
| `include/engine/pipeline/linking/pipeline_linker.hpp`   | `src/linking/pipeline_linker.cpp`          |

### Key Linking Patterns

```cpp
// Static link: two elements with compatible fixed pads
bool PipelineLinker::link(GstElement* src, GstElement* sink) {
    if (!gst_element_link(src, sink)) {
        LOG_E("Failed to link {} → {}",
              GST_ELEMENT_NAME(src), GST_ELEMENT_NAME(sink));
        return false;
    }
    return true;
}

// Dynamic link: nvmultiurisrcbin → muxer (request pads)
// Registered via g_signal_connect(srcbin, "pad-added", G_CALLBACK(on_pad_added), muxer)
static void on_pad_added(GstElement* src, GstPad* new_pad, GstElement* muxer) {
    engine::core::utils::GstPadPtr sink_pad(
        gst_element_get_request_pad(muxer, "sink_%u"), gst_object_unref);
    if (!sink_pad) { LOG_E("Could not get muxer sink pad"); return; }
    if (gst_pad_link(new_pad, sink_pad.get()) != GST_PAD_LINK_OK) {
        LOG_E("Dynamic pad link failed");
    }
}
```

---

## Section 3.6 — Probes

### Files to Create

| Header                                                               | Source                                              |
| -------------------------------------------------------------------- | --------------------------------------------------- |
| `include/engine/pipeline/probes/probe_handler_manager.hpp`           | `src/probes/probe_handler_manager.cpp`              |
| `include/engine/pipeline/probes/smart_record_probe_handler.hpp`      | `src/probes/smart_record_probe_handler.cpp`         |
| `include/engine/pipeline/probes/crop_object_handler.hpp`             | `src/probes/crop_object_handler.cpp`                |
| `include/engine/pipeline/probes/class_id_namespace_handler.hpp`      | `src/probes/class_id_namespace_handler.cpp`         |

### Probe Pattern (NvDs Metadata)

```cpp
static GstPadProbeReturn on_buffer_probe(
    GstPad* /*pad*/, GstPadProbeInfo* info, gpointer user_data)
{
    GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
    NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buf);
    if (!batch_meta) return GST_PAD_PROBE_OK;

    for (NvDsMetaList* l_frame = batch_meta->frame_meta_list;
         l_frame; l_frame = l_frame->next)
    {
        auto* frame_meta = static_cast<NvDsFrameMeta*>(l_frame->data);
        for (NvDsMetaList* l_obj = frame_meta->obj_meta_list;
             l_obj; l_obj = l_obj->next)
        {
            auto* obj = static_cast<NvDsObjectMeta*>(l_obj->data);
            // obj->class_id, obj->confidence, obj->object_id
            // obj->rect_params.left/top/width/height
        }
    }
    return GST_PAD_PROBE_OK;
}
```

---

## Section 3.7 — Services (Optional In-Pipeline)

If the `ExtProcSvc` (external processor) is needed:

| Header                                                 | Source                                      |
| ------------------------------------------------------ | ------------------------------------------- |
| `include/engine/pipeline/services/ext_proc_svc.hpp`   | `src/services/ext_proc_svc.cpp`             |

---

## CMakeLists.txt

```cmake
# pipeline/CMakeLists.txt
file(GLOB_RECURSE PIPELINE_SOURCES "src/*.cpp")

add_library(vms_engine_pipeline STATIC ${PIPELINE_SOURCES})

target_include_directories(vms_engine_pipeline
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${DEEPSTREAM_DIR}/sources/includes
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../deps/deepstream/includes
)

target_link_libraries(vms_engine_pipeline
    PUBLIC  vms_engine_core
    PUBLIC  PkgConfig::GST
    PUBLIC  PkgConfig::GST_BASE
    PUBLIC  PkgConfig::GST_VIDEO
    PUBLIC  PkgConfig::GLIB2
    PRIVATE ${NVDS_LIBS}
    PRIVATE nlohmann_json::nlohmann_json
)

set_target_properties(vms_engine_pipeline PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
```

---

## File Count Summary

| Category            | Headers | Sources | Notes                                     |
| ------------------- | ------- | ------- | ----------------------------------------- |
| Root managers        | 5       | 5       | PipelineManager, BuilderFactory, etc.     |
| Block builders       | 4       | 4       | One per build phase                       |
| Element builders     | 13      | 13      | One per GStreamer element type            |
| Event handlers       | 5       | 4       | + object_crop.hpp (header-only)           |
| Linking              | 1       | 1       |                                           |
| Probes               | 4       | 4       |                                           |
| Services (optional)  | 1       | 1       |                                           |
| **Total**            | **33**  | **32**  | ~65 files + headers                       |

---

## Verification

```bash
# Inside container: docker compose exec app bash
cd /opt/vms_engine

# 1. Compile pipeline library only
cmake --build build --target vms_engine_pipeline -- -j5

# 2. Check no layer dependency violations
grep -rn '#include "engine/infrastructure\|#include "engine/domain\|#include "engine/services' \
    pipeline/ --include="*.hpp" --include="*.cpp" && echo "FAIL" || echo "PASS"

# 3. Check namespace consistency
grep -rL "engine::pipeline" pipeline/include/ --include="*.hpp" | head -10

# 4. Check RAII usage in builders
grep -rn "gst_element_factory_make" pipeline/src/builders/ --include="*.cpp"
# Every occurrence should be paired with make_gst_element() or followed by RAII guard

# 5. Build full binary
cmake --build build -- -j5
ls -la build/bin/vms_engine
```

---

## Checklist

- [ ] 5 root-level manager class headers + sources
- [ ] 4 block builder headers + sources
- [ ] 13 element builder headers + sources (source through demuxer)
- [ ] `QueueBuilder` takes `QueueConfig` directly (not `PipelineConfig`)
- [ ] ALL other builders take `const PipelineConfig& config, int index = 0`
- [ ] All builders use `make_gst_element()` RAII guard; call `elem.release()` after `gst_bin_add()`
- [ ] All pad accesses wrapped with `GstPadPtr`
- [ ] `PipelineManager` uses `GstPipelineOwner` RAII (set_state NULL before unref)
- [ ] Event handlers take `IMessageProducer*` via constructor (no static coupling)
- [ ] `HandlerManager` implements `IHandlerManager` from core
- [ ] `ProbeHandlerManager` registers pad probes based on `EventHandlerConfig`
- [ ] All files in `engine::pipeline` namespace
- [ ] All includes use `"engine/pipeline/..."` or `"engine/core/..."` paths
- [ ] `pipeline/CMakeLists.txt` defines `vms_engine_pipeline` static library
- [ ] `vms_engine_pipeline` only links against `vms_engine_core` (no infrastructure, no domain)
