---
goal: "Plan 03 — Pipeline Layer: Builders, Manager, Probes, Event Handlers"
version: "1.0"
date_created: "2025-01-15"
last_updated: "2025-07-17"
owner: "VMS Engine Team"
status: "Planned"
tags: [pipeline, builders, gstreamer, deepstream, probes, event-handlers, raii]
---

# Plan 03 — Pipeline Layer (Builders, Manager, Probes, Event Handlers)

![Status: Planned](https://img.shields.io/badge/status-Planned-blue)

Create all files in `pipeline/` from scratch.
This layer implements the core interfaces defined in Plan 02 using GStreamer/DeepStream SDK.
No migration from lantanav2 — all files are new creations.

---

## 1. Requirements & Constraints

- **REQ-001**: Plan 02 completed (`vms_engine_core` compiles — all `I*` interfaces and config types defined).
- **REQ-002**: All builders follow the signature `GstElement* build(const PipelineConfig& config, int index = 0)`.
- **REQ-003**: All builders use `make_gst_element()` RAII guard; call `elem.release()` after `gst_bin_add()`.
- **REQ-004**: `PipelineManager` implements `IPipelineManager` with state machine and GstBus watch.
- **REQ-005**: Event handlers take `IMessageProducer*` + `IStorageManager*` via constructor (no static coupling).
- **REQ-006**: Pad probes iterate NvDs metadata (`NvDsBatchMeta` → `NvDsFrameMeta` → `NvDsObjectMeta`).
- **SEC-001**: No `ip_address` property on `nvmultiurisrcbin` — DeepStream 8.0 SIGSEGV.
- **CON-001**: Pipeline layer depends only on `vms_engine_core` — no domain, no infrastructure.
- **CON-002**: All files use `engine::pipeline::*` namespace.
- **CON-003**: `QueueBuilder` takes `QueueConfig` directly (not `PipelineConfig`) — queues are resolved by block builders.
- **GUD-001**: All pad accesses wrapped with `GstPadPtr` RAII.
- **GUD-002**: Block builders communicate via `tails_` map (`std::unordered_map<std::string, GstElement*>`).
- **PAT-001**: Full Config Pattern — builder receives entire `PipelineConfig`, accesses its relevant section via index.

---

## 2. Implementation Steps

### Architecture Overview

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

### Config Section Access Map

| Builder            | Config access                                 | Type                      |
| ------------------ | --------------------------------------------- | ------------------------- |
| `SourceBuilder`    | `config.sources`                              | `SourcesConfig`           |
| `MuxerBuilder`     | `config.sources` (batch_size, width, height)  | `SourcesConfig`           |
| `QueueBuilder`     | `QueueConfig` directly (not PipelineConfig)   | `QueueConfig`             |
| `InferBuilder`     | `config.processing.elements[index]`           | `ProcessingElementConfig` |
| `TrackerBuilder`   | `config.processing.elements[index]`           | `ProcessingElementConfig` |
| `AnalyticsBuilder` | `config.processing.elements[index]`           | `ProcessingElementConfig` |
| `TilerBuilder`     | `config.visuals.elements[index]`              | `VisualsElementConfig`    |
| `OsdBuilder`       | `config.visuals.elements[index]`              | `VisualsElementConfig`    |
| `EncoderBuilder`   | `config.outputs[outputIdx].elements[elemIdx]` | `OutputElementConfig`     |
| `SinkBuilder`      | `config.outputs[outputIdx].elements[elemIdx]` | `OutputElementConfig`     |
| `MsgconvBuilder`   | `config.outputs[outputIdx].elements[elemIdx]` | `OutputElementConfig`     |
| `MsgbrokerBuilder` | `config.outputs[outputIdx].elements[elemIdx]` | `OutputElementConfig`     |
| `DemuxerBuilder`   | `config.sources` (num sources for pads)       | `SourcesConfig`           |

### Phase 1 — Root Manager Classes

**GOAL-001**: Create PipelineManager, RuntimeStreamManager, SmartRecordController, BuilderFactory, ConfigValidator.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-001 | Create `PipelineManager` implementing `IPipelineManager` with GstBus watch and state machine | ☐ | |
| TASK-002 | Create `RuntimeStreamManager` for runtime camera add/remove | ☐ | |
| TASK-003 | Create `SmartRecordController` wrapping NvDsSR API | ☐ | |
| TASK-004 | Create `BuilderFactory` implementing `IBuilderFactory` (type string → builder lookup) | ☐ | |
| TASK-005 | Create `ConfigValidator` for pipeline-specific validation | ☐ | |

**`PipelineManager` — Key Notes:**

- Owns `GstElement* pipeline_` as a `GstPipelineOwner` RAII wrapper (see RAII.md)
- Sets up GstBus watch: `gst_bus_add_watch(bus, on_bus_message, this)`
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

### Phase 2 — Block Builders (Pipeline Build Phases)

**GOAL-002**: Create 4 block builders (one per pipeline phase) with `tails_` map linkage.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-006 | Create `SourceBlockBuilder` — Phase 1: nvmultiurisrcbin + ghost pad | ☐ | |
| TASK-007 | Create `ProcessingBlockBuilder` — Phase 2: inference chain (PGIE → tracker → SGIE → analytics) | ☐ | |
| TASK-008 | Create `VisualsBlockBuilder` — Phase 3: tiler + OSD (if visuals.enable=true) | ☐ | |
| TASK-009 | Create `OutputsBlockBuilder` — Phase 4: encoders + sinks (one bin per output) | ☐ | |

**Build Phase Overview:**

```
Phase 1 — SourceBlockBuilder:
    GstBin "sources_bin":
        nvmultiurisrcbin (no ip-address/port — DS8 SIGSEGV)
    ghost src pad → tails_["src"] = sources_bin

Phase 2 — ProcessingBlockBuilder:
    GstBin "processing_bin":
        [queue?] → nvinfer(PGIE) → [queue?] → nvtracker → [queue?] → nvinfer(SGIE)* → ...
        → nvdsanalytics?
    ghost src pad → tails_["src"] = processing_bin

Phase 3 — VisualsBlockBuilder:
    GstBin "visuals_bin":
        [queue?] → nvmultistreamtiler → [queue?] → nvdsosd
    ghost src pad → tails_["src"] = visuals_bin

Phase 4 — OutputsBlockBuilder:
    For each output in config.outputs:
      GstBin "output_bin_{id}":
        [queue?] → nvv4l2h264enc/265enc → [queue?] → sink
        OR [queue?] → nvmsgconv → nvmsgbroker
    ghost src pad (if needed) per output bin

Inter-bin linking: gst_element_link(bin_a, bin_b) — ghost pads handle the rest.
No output_queue at block boundary — queue is per-element (queue: {} inline).
```

**`tails_` Map Pattern:**

```cpp
// Each block builder updates this map after building its phase
// key = descriptive name, value = GstElement* (tail of this phase's chain)
std::unordered_map<std::string, GstElement*> tails_;

// After Phase 1:  tails_["src"]  = last element of source block
// After Phase 2:  tails_["proc"] = last element of processing block
// After Phase 3:  tails_["vis"]  = last element of visuals block
// Phase 4 reads tails_["proc"] or tails_["vis"] to connect sinks
```

### Phase 3 — Element Builders

**GOAL-003**: Create 13 element builders (one per GStreamer element type).

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-010 | Create `SourceBuilder` — nvmultiurisrcbin (3 property groups, smart record) | ☐ | |
| TASK-011 | Create `MuxerBuilder` — nvstreammux (fallback/explicit) | ☐ | |
| TASK-012 | Create `QueueBuilder` — GStreamer queue (takes `QueueConfig` directly) | ☐ | |
| TASK-013 | Create `InferBuilder` — nvinfer (PGIE + SGIE, with `infer-on-gie-id` for SGIE) | ☐ | |
| TASK-014 | Create `TrackerBuilder` — nvtracker | ☐ | |
| TASK-015 | Create `AnalyticsBuilder` — nvdsanalytics | ☐ | |
| TASK-016 | Create `TilerBuilder` — nvmultistreamtiler | ☐ | |
| TASK-017 | Create `OsdBuilder` — nvdsosd | ☐ | |
| TASK-018 | Create `EncoderBuilder` — nvv4l2h264enc / nvv4l2h265enc | ☐ | |
| TASK-019 | Create `SinkBuilder` — rtspclientsink / fakesink / filesink | ☐ | |
| TASK-020 | Create `MsgconvBuilder` — nvmsgconv | ☐ | |
| TASK-021 | Create `MsgbrokerBuilder` — nvmsgbroker | ☐ | |
| TASK-022 | Create `DemuxerBuilder` — nvstreamdemux | ☐ | |

**Standard Builder Template:**

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
    explicit InferBuilder(GstElement* bin);
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

    if (elem_cfg.process_mode == 2) {
        g_object_set(G_OBJECT(elem.get()),
            "operate-on-gie-id", static_cast<gint>(elem_cfg.operate_on_gie_id),
            nullptr);
    }

    if (!gst_bin_add(GST_BIN(bin_), elem.get())) {
        LOG_E("Failed to add nvinfer '{}' to bin", id);
        return nullptr;
    }

    LOG_I("Built nvinfer '{}' (mode={})", id, elem_cfg.process_mode);
    return elem.release();
}

} // namespace engine::pipeline::builders
```

**QueueBuilder — Takes `QueueConfig` Directly:**

```cpp
// pipeline/include/engine/pipeline/builders/queue_builder.hpp
#pragma once
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>

namespace engine::pipeline::builders {

class QueueBuilder {
public:
    explicit QueueBuilder(GstElement* bin);
    GstElement* build(const engine::core::config::QueueConfig& cfg,
                      const std::string& name);

private:
    GstElement* bin_ = nullptr;
};

} // namespace engine::pipeline::builders
```

### Phase 4 — Event Handlers

**GOAL-004**: Create event handlers with DI pattern and probe registration.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-023 | Create `HandlerManager` implementing `IHandlerManager` from core | ☐ | |
| TASK-024 | Create `SmartRecordHandler` with IMessageProducer* + IStorageManager* injection | ☐ | |
| TASK-025 | Create `CropDetectedObjHandler` | ☐ | |
| TASK-026 | Create `ExtProcHandler` | ☐ | |
| TASK-027 | Create `object_crop.hpp` header-only utility | ☐ | |

**Handler DI Pattern:**

```cpp
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

**Pad Guard for Probe Registration:**

```cpp
void SmartRecordHandler::register_probe(GstElement* element) {
    engine::core::utils::GstPadPtr pad(
        gst_element_get_static_pad(element, "sink"), gst_object_unref);
    if (!pad) { LOG_E("Pad not found"); return; }
    gst_pad_add_probe(pad.get(), GST_PAD_PROBE_TYPE_BUFFER,
                      on_probe_buffer, this, nullptr);
}  // gst_object_unref(pad) called automatically
```

### Phase 5 — Linking & Probes

**GOAL-005**: Create PipelineLinker and probe handler manager.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-028 | Create `PipelineLinker` with static + dynamic pad-added linking | ☐ | |
| TASK-029 | Create `ProbeHandlerManager` — pad probe registration based on EventHandlerConfig | ☐ | |
| TASK-030 | Create `SmartRecordProbeHandler` | ☐ | |
| TASK-031 | Create `CropObjectHandler` (probe) | ☐ | |
| TASK-032 | Create `ClassIdNamespaceHandler` for multi-GIE class_id offset/restore | ☐ | |

**Linking Patterns:**

```cpp
// Static link
bool PipelineLinker::link(GstElement* src, GstElement* sink) {
    if (!gst_element_link(src, sink)) {
        LOG_E("Failed to link {} → {}", GST_ELEMENT_NAME(src), GST_ELEMENT_NAME(sink));
        return false;
    }
    return true;
}

// Dynamic link: nvmultiurisrcbin → muxer (request pads)
static void on_pad_added(GstElement* src, GstPad* new_pad, GstElement* muxer) {
    engine::core::utils::GstPadPtr sink_pad(
        gst_element_get_request_pad(muxer, "sink_%u"), gst_object_unref);
    if (!sink_pad) { LOG_E("Could not get muxer sink pad"); return; }
    if (gst_pad_link(new_pad, sink_pad.get()) != GST_PAD_LINK_OK) {
        LOG_E("Dynamic pad link failed");
    }
}
```

**Probe Pattern (NvDs Metadata):**

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

### Phase 6 — Services & Build Integration

**GOAL-006**: Optional in-pipeline services + CMakeLists.txt.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-033 | Create `ExtProcSvc` (optional external processor service) | ☐ | |
| TASK-034 | Create `pipeline/CMakeLists.txt` defining `vms_engine_pipeline` static library | ☐ | |

**CMakeLists.txt:**

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

| Category            | Headers | Sources | Notes                                 |
| ------------------- | ------- | ------- | ------------------------------------- |
| Root managers       | 5       | 5       | PipelineManager, BuilderFactory, etc. |
| Block builders      | 4       | 4       | One per build phase                   |
| Element builders    | 13      | 13      | One per GStreamer element type         |
| Event handlers      | 5       | 4       | + object_crop.hpp (header-only)       |
| Linking             | 1       | 1       |                                       |
| Probes              | 4       | 4       |                                       |
| Services (optional) | 1       | 1       |                                       |
| **Total**           | **33**  | **32**  | ~65 files + headers                   |

---

## 3. Alternatives

- **ALT-001**: Config slices per builder (rejected — full `PipelineConfig` is simpler and avoids inter-builder data passing).
- **ALT-002**: Single monolithic pipeline builder (rejected — 4-phase block builders with `tails_` map scales better and isolates concerns).
- **ALT-003**: Event handlers as static callbacks (rejected — constructor DI with `IMessageProducer*` enables testing and decouples from infrastructure).

---

## 4. Dependencies

- **DEP-001**: Plan 02 completed — `vms_engine_core` with all `I*` interfaces and config types.
- **DEP-002**: DeepStream SDK 8.0 — `nvmultiurisrcbin`, `nvinfer`, `nvtracker`, NvDs metadata headers.
- **DEP-003**: GStreamer 1.0 — `gst_element_factory_make`, `gst_bin_add`, `gst_element_link`, pad probes.
- **DEP-004**: nlohmann/json v3.11.3 — used in event handler JSON payloads.
- **DEP-005**: spdlog v1.14.1 — LOG_* macros from core.

---

## 5. Files

| ID | File Path | Description |
|----|-----------|-------------|
| FILE-001 | `pipeline/include/engine/pipeline/pipeline_manager.hpp` | IPipelineManager implementation |
| FILE-002 | `pipeline/include/engine/pipeline/runtime_stream_manager.hpp` | Runtime camera add/remove |
| FILE-003 | `pipeline/include/engine/pipeline/smart_record_controller.hpp` | NvDsSR API wrapper |
| FILE-004 | `pipeline/include/engine/pipeline/builder_factory.hpp` | IBuilderFactory implementation |
| FILE-005 | `pipeline/include/engine/pipeline/config_validator.hpp` | Pipeline config validation |
| FILE-006 | `pipeline/include/engine/pipeline/block_builders/source_block_builder.hpp` | Phase 1: source bin |
| FILE-007 | `pipeline/include/engine/pipeline/block_builders/processing_block_builder.hpp` | Phase 2: inference chain |
| FILE-008 | `pipeline/include/engine/pipeline/block_builders/visuals_block_builder.hpp` | Phase 3: tiler + OSD |
| FILE-009 | `pipeline/include/engine/pipeline/block_builders/outputs_block_builder.hpp` | Phase 4: encoders + sinks |
| FILE-010 | `pipeline/include/engine/pipeline/builders/source_builder.hpp` | nvmultiurisrcbin |
| FILE-011 | `pipeline/include/engine/pipeline/builders/muxer_builder.hpp` | nvstreammux |
| FILE-012 | `pipeline/include/engine/pipeline/builders/queue_builder.hpp` | GStreamer queue |
| FILE-013 | `pipeline/include/engine/pipeline/builders/infer_builder.hpp` | nvinfer (PGIE + SGIE) |
| FILE-014 | `pipeline/include/engine/pipeline/builders/tracker_builder.hpp` | nvtracker |
| FILE-015 | `pipeline/include/engine/pipeline/builders/analytics_builder.hpp` | nvdsanalytics |
| FILE-016 | `pipeline/include/engine/pipeline/builders/tiler_builder.hpp` | nvmultistreamtiler |
| FILE-017 | `pipeline/include/engine/pipeline/builders/osd_builder.hpp` | nvdsosd |
| FILE-018 | `pipeline/include/engine/pipeline/builders/encoder_builder.hpp` | nvv4l2h264enc/265enc |
| FILE-019 | `pipeline/include/engine/pipeline/builders/sink_builder.hpp` | rtspclientsink/fakesink |
| FILE-020 | `pipeline/include/engine/pipeline/builders/msgconv_builder.hpp` | nvmsgconv |
| FILE-021 | `pipeline/include/engine/pipeline/builders/msgbroker_builder.hpp` | nvmsgbroker |
| FILE-022 | `pipeline/include/engine/pipeline/builders/demuxer_builder.hpp` | nvstreamdemux |
| FILE-023 | `pipeline/include/engine/pipeline/event_handlers/handler_manager.hpp` | IHandlerManager impl |
| FILE-024 | `pipeline/include/engine/pipeline/event_handlers/smart_record_handler.hpp` | Smart record handler |
| FILE-025 | `pipeline/include/engine/pipeline/event_handlers/crop_detected_obj_handler.hpp` | Crop on detection |
| FILE-026 | `pipeline/include/engine/pipeline/event_handlers/ext_proc_handler.hpp` | External processor |
| FILE-027 | `pipeline/include/engine/pipeline/event_handlers/object_crop.hpp` | Header-only crop util |
| FILE-028 | `pipeline/include/engine/pipeline/linking/pipeline_linker.hpp` | Static + dynamic linking |
| FILE-029 | `pipeline/include/engine/pipeline/probes/probe_handler_manager.hpp` | Pad probe registration |
| FILE-030 | `pipeline/include/engine/pipeline/probes/smart_record_probe_handler.hpp` | SmartRecord probe |
| FILE-031 | `pipeline/include/engine/pipeline/probes/crop_object_handler.hpp` | CropObject probe |
| FILE-032 | `pipeline/include/engine/pipeline/probes/class_id_namespace_handler.hpp` | Multi-GIE class_id |
| FILE-033 | `pipeline/include/engine/pipeline/services/ext_proc_svc.hpp` | External processor svc |
| FILE-034 | `pipeline/CMakeLists.txt` | Build config for vms_engine_pipeline |

---

## 6. Testing & Verification

- **TEST-001**: Compile pipeline library only — `cmake --build build --target vms_engine_pipeline -- -j5`.
- **TEST-002**: No layer dependency violations — `grep -rn '#include "engine/infrastructure\|#include "engine/domain' pipeline/ --include="*.hpp" --include="*.cpp" && echo "FAIL" || echo "PASS"`.
- **TEST-003**: Namespace consistency — `grep -rL "engine::pipeline" pipeline/include/ --include="*.hpp" | head -10`.
- **TEST-004**: RAII usage in builders — all `gst_element_factory_make` uses `make_gst_element()`.
- **TEST-005**: Full binary builds — `cmake --build build -- -j5 && ls -la build/bin/vms_engine`.

---

## 7. Risks & Assumptions

- **RISK-001**: ~65 files is a large initial creation scope; mitigated by following a consistent template for all builders and parallel development of independent builder classes.
- **RISK-002**: Dynamic pad linking (nvmultiurisrcbin → muxer) is timing-sensitive; mitigated by proven GStreamer `pad-added` signal pattern.
- **RISK-003**: NvDs metadata API changes between DeepStream versions; mitigated by pinning to SDK 8.0.
- **ASSUMPTION-001**: All builders follow identical RAII guard pattern (`make_gst_element` → configure → `gst_bin_add` → `release()`).
- **ASSUMPTION-002**: Block builders can be developed and tested independently using stub configs.
- **ASSUMPTION-003**: DeepStream SDK 8.0 is installed in the container at `/opt/nvidia/deepstream/deepstream`.

---

## 8. Related Specifications

- [Plan 02 — Core Layer](02_core_layer.md) (prerequisite interfaces)
- [Plan 04 — Domain Layer](04_domain_layer.md)
- [Pipeline Building Guide](../../docs/architecture/deepstream/03_pipeline_building.md)
- [Linking System](../../docs/architecture/deepstream/04_linking_system.md)
- [Event Handlers & Probes](../../docs/architecture/deepstream/07_event_handlers_probes.md)
- [SmartRecord Probe](../../docs/architecture/probes/smart_record_probe_handler.md)
- [CropObject Probe](../../docs/architecture/probes/crop_object_handler.md)
- [ClassId Namespacing](../../docs/architecture/probes/class_id_namespacing_handler.md)
- [RAII Guide](../../docs/architecture/RAII.md)
- [AGENTS.md](../../AGENTS.md)
