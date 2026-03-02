# Plan 03 — Pipeline Layer (DeepStream Backend → Pipeline Module)

> Migrate all files from `lantanav2/backends/deepstream/` → `vms-engine/pipeline/`.
> This is the **largest sub-plan** (~73 files). DeepStream is now the only backend — no guards, no variants, no `ds_` prefix.

---

## Prerequisites

- Plan 01 completed (scaffold exists)
- Plan 02 completed (core interfaces compile)

## Deliverables

- [ ] All headers migrated under `pipeline/include/engine/pipeline/`
- [ ] All sources migrated under `pipeline/src/`
- [ ] `ds_` prefix removed from all filenames and class names
- [ ] `lantana::backends::deepstream::` → `engine::pipeline::` namespace
- [ ] v1/v2 duplicates consolidated (keep v2, remove v1)
- [ ] Handler Redis coupling removed (use injected `IMessageProducer`)
- [ ] `pipeline/CMakeLists.txt` updated
- [ ] Pipeline library compiles

---

## v1/v2 Consolidation Strategy

Several files have `_v2` variants. **Always keep v2, rename it to drop `_v2` suffix, delete v1.**

| v1 (delete)                      | v2 → rename to            | Notes                               |
| --------------------------------- | ------------------------- | ------------------------------------ |
| `crop_object_handler.hpp/cpp`    | `crop_object_handler.hpp` | Upgraded cropping logic              |
| `smart_record_handler.hpp/cpp`   | `smart_record_handler.hpp`| Improved recording control           |
| `ext_proc_svc.hpp/cpp`           | `ext_proc_svc.hpp/cpp`    | External processing service          |
| `outputs_builder.hpp`            | `outputs_builder.hpp`     | Refactored output bin construction   |

---

## File Migration Table

### 3.1 — Root-Level Manager Classes

| Source (lantanav2)                        | Target (vms-engine)                          | Rename                        |
| ----------------------------------------- | -------------------------------------------- | ------------------------------ |
| `ds_builder_factory.hpp/cpp`             | `builder_factory.hpp/cpp`                    | Drop `ds_`; class `BuilderFactory` |
| `ds_pipeline_manager.hpp/cpp`            | `pipeline_manager.hpp/cpp`                   | Drop `ds_`; class `PipelineManager` |
| `ds_runtime_stream_manager.hpp/cpp`      | `runtime_stream_manager.hpp/cpp`             | Drop `ds_`; class `RuntimeStreamManager` |
| `ds_smart_record_controller.hpp/cpp`     | `smart_record_controller.hpp/cpp`            | Drop `ds_`; class `SmartRecordController` |
| `link_manager.hpp`                        | `link_manager.hpp`                            | Namespace only                |
| `queue_manager.hpp`                       | `queue_manager.hpp`                           | Namespace only                |

Headers go to `pipeline/include/engine/pipeline/`, sources to `pipeline/src/`.

### 3.2 — Block Builders (High-Level Pipeline Construction)

| Source                                          | Target                                              | Rename         |
| ----------------------------------------------- | --------------------------------------------------- | --------------- |
| `block_builders/base_builder.hpp`              | `block_builders/base_builder.hpp`                   | Namespace       |
| `block_builders/pipeline_builder.hpp`          | `block_builders/pipeline_builder.hpp`               | Namespace       |
| `block_builders/source_builder.hpp`            | `block_builders/source_builder.hpp`                 | Namespace       |
| `block_builders/processing_builder.hpp`        | `block_builders/processing_builder.hpp`             | Namespace       |
| `block_builders/visuals_builder.hpp`           | `block_builders/visuals_builder.hpp`                | Namespace       |
| `block_builders/standalone_builder.hpp`        | `block_builders/standalone_builder.hpp`             | Namespace       |

### 3.3 — Outputs Builder (Sub-Module)

| Source                                                         | Target                                                    | Rename / Action     |
| -------------------------------------------------------------- | --------------------------------------------------------- | -------------------- |
| `block_builders/outputs_builder/outputs_builder.hpp` (v1)     | **DELETE**                                                 | Replaced by v2       |
| `block_builders/outputs_builder/outputs_builder_v2.hpp`       | `block_builders/outputs_builder/outputs_builder.hpp`       | Rename, drop `_v2`  |
| `block_builders/outputs_builder/outputs_bin_builder.hpp`      | `block_builders/outputs_builder/outputs_bin_builder.hpp`   | Namespace            |
| `block_builders/outputs_builder/output_branch_builder.hpp`    | `block_builders/outputs_builder/output_branch_builder.hpp` | Namespace            |
| `block_builders/outputs_builder/tee_manager.hpp`              | `block_builders/outputs_builder/tee_manager.hpp`           | Namespace            |
| `block_builders/outputs_builder/fakesink_handler.hpp`         | `block_builders/outputs_builder/fakesink_handler.hpp`      | Namespace            |

### 3.4 — Element Builders (GStreamer Element Configuration)

All `ds_*_builder` files have their `ds_` prefix removed:

| Source                                | Target                            | Class Rename                       |
| -------------------------------------- | --------------------------------- | ----------------------------------- |
| `builders/ds_analytics_builder.hpp/cpp`      | `builders/analytics_builder.hpp/cpp`    | `DsAnalyticsBuilder` → `AnalyticsBuilder` |
| `builders/ds_demuxer_builder.hpp/cpp`        | `builders/demuxer_builder.hpp/cpp`      | `DsDemuxerBuilder` → `DemuxerBuilder`     |
| `builders/ds_encoder_builder.hpp/cpp`        | `builders/encoder_builder.hpp/cpp`      | `DsEncoderBuilder` → `EncoderBuilder`     |
| `builders/ds_infer_builder.hpp/cpp`          | `builders/infer_builder.hpp/cpp`        | `DsInferBuilder` → `InferBuilder`         |
| `builders/ds_msgconv_broker_builder.hpp/cpp` | `builders/msgconv_broker_builder.hpp/cpp`| `DsMsgconvBrokerBuilder` → `MsgconvBrokerBuilder` |
| `builders/ds_muxer_builder.hpp/cpp`          | `builders/muxer_builder.hpp/cpp`        | `DsMuxerBuilder` → `MuxerBuilder`         |
| `builders/ds_osd_builder.hpp/cpp`            | `builders/osd_builder.hpp/cpp`          | `DsOsdBuilder` → `OsdBuilder`             |
| `builders/ds_queue_builder.hpp/cpp`          | `builders/queue_builder.hpp/cpp`        | `DsQueueBuilder` → `QueueBuilder`         |
| `builders/ds_sink_builder.hpp/cpp`           | `builders/sink_builder.hpp/cpp`         | `DsSinkBuilder` → `SinkBuilder`           |
| `builders/ds_smart_record_builder.hpp/cpp`   | `builders/smart_record_builder.hpp/cpp` | `DsSmartRecordBuilder` → `SmartRecordBuilder` |
| `builders/ds_source_builder.hpp/cpp`         | `builders/source_builder.hpp/cpp`       | `DsSourceBuilder` → `SourceBuilder`       |
| `builders/ds_tiler_builder.hpp/cpp`          | `builders/tiler_builder.hpp/cpp`        | `DsTilerBuilder` → `TilerBuilder`         |
| `builders/ds_tracker_builder.hpp/cpp`        | `builders/tracker_builder.hpp/cpp`      | `DsTrackerBuilder` → `TrackerBuilder`     |

**Additional src-only files** (no matching header — implementation helpers):

| Source                                                | Target                                            | Notes                  |
| ------------------------------------------------------ | ------------------------------------------------- | ----------------------- |
| `src/builders/processing_flow_bin_builder.cpp`        | `src/builders/processing_flow_bin_builder.cpp`     | Namespace + includes   |
| `src/builders/source_bin_builder.cpp`                 | `src/builders/source_bin_builder.cpp`              | Namespace + includes   |
| `src/builders/ds_smuxer_builder.cpp`                  | `src/builders/smuxer_builder.cpp`                  | Drop `ds_` prefix      |

### 3.5 — Config (Validator)

| Source                                    | Target                                  | Rename                          |
| ----------------------------------------- | --------------------------------------- | -------------------------------- |
| `config/ds_config_validator.hpp/cpp`     | `config/config_validator.hpp/cpp`       | Drop `ds_`; class `ConfigValidator` |

### 3.6 — Event Handlers

| Source                                              | Target                                          | Action                  |
| ----------------------------------------------------- | ----------------------------------------------- | ------------------------ |
| `event_handlers/crop_detected_obj_handler.hpp/cpp` | `event_handlers/crop_detected_obj_handler.hpp/cpp` | Namespace; inject `IMessageProducer` |
| `event_handlers/ext_proc_handler.hpp/cpp`          | `event_handlers/ext_proc_handler.hpp/cpp`          | Namespace; inject `IMessageProducer` |
| `event_handlers/handler_manager.hpp/cpp`           | `event_handlers/handler_manager.hpp/cpp`           | Namespace               |
| `event_handlers/object_crop.hpp`                    | `event_handlers/object_crop.hpp`                    | Namespace (utility header) |
| `event_handlers/smart_record_handler.hpp/cpp` (v1) | **DELETE**                                          | Replaced by v2          |
| `event_handlers/smart_record_handler_v2.hpp/cpp`   | `event_handlers/smart_record_handler.hpp/cpp`       | Rename, drop `_v2`; inject `IMessageProducer` |

### 3.7 — Linking

| Source                                  | Target                                  | Changes    |
| ---------------------------------------- | ---------------------------------------- | ----------- |
| `linking/pipeline_linker.hpp/cpp`      | `linking/pipeline_linker.hpp/cpp`        | Namespace  |

### 3.8 — Probes

| Source                                               | Target                                          | Action                  |
| ------------------------------------------------------ | ----------------------------------------------- | ------------------------ |
| `probes/class_id_namespace_handler.hpp/cpp`          | `probes/class_id_namespace_handler.hpp/cpp`      | Namespace               |
| `probes/crop_object_handler.hpp/cpp` (v1)            | **DELETE**                                        | Replaced by v2          |
| `probes/crop_object_handler_v2.hpp/cpp`              | `probes/crop_object_handler.hpp/cpp`              | Rename, drop `_v2`     |
| `probes/probe_handler_manager.hpp/cpp`               | `probes/probe_handler_manager.hpp/cpp`            | Namespace               |
| `probes/smart_record_probe_handler.hpp/cpp`          | `probes/smart_record_probe_handler.hpp/cpp`       | Namespace               |

### 3.9 — Services (in-backend)

| Source                                    | Target                                  | Action                |
| ----------------------------------------- | --------------------------------------- | ---------------------- |
| `services/ext_proc_svc.hpp/cpp` (v1)    | **DELETE**                               | Replaced by v2        |
| `services/ext_proc_svc_v2.hpp/cpp`      | `services/ext_proc_svc.hpp/cpp`          | Rename, drop `_v2`   |

### 3.10 — Utils

| Source                             | Target                          | Changes    |
| ----------------------------------- | ------------------------------- | ----------- |
| `utils/uuid_manager.hpp/cpp`      | `utils/uuid_manager.hpp/cpp`    | Namespace  |

---

## Critical Rework During Migration

### Rework 1: Handler Dependency Injection

In lantanav2, event handlers access Redis through a static `IHandler::redis_producer_` (removed in Plan 02). Each handler now receives `IMessageProducer*` via constructor:

**Before:**
```cpp
class CropDetectedObjHandler : public IHandler {
    void handle(/* ... */) {
        // Uses static redis_producer_
        IHandler::redis_producer_->publish("channel", msg);
    }
};
```

**After:**
```cpp
class CropDetectedObjHandler : public engine::core::handlers::IHandler {
public:
    explicit CropDetectedObjHandler(engine::core::messaging::IMessageProducer* producer)
        : producer_(producer) {}

    void handle(/* ... */) {
        if (producer_) {
            producer_->publish("channel", msg);
        }
    }
private:
    engine::core::messaging::IMessageProducer* producer_{nullptr};
};
```

Apply to all event handlers: `crop_detected_obj_handler`, `ext_proc_handler`, `smart_record_handler`.

### Rework 2: Drop #ifdef Backend Guards

Remove all conditional compilation guards:

```cpp
// DELETE these patterns
#ifdef LANTANA_WITH_DEEPSTREAM
#endif

#ifdef LANTANA_WITH_DLSTREAMER
// ... entire block
#endif
```

All code is now unconditionally compiled.

### Rework 3: Update Internal Cross-References

After file renames, update all internal `#include` statements within pipeline code:

```bash
cd vms-engine/pipeline

# Update includes from old backend path
find . -name "*.hpp" -o -name "*.cpp" | xargs sed -i \
    -e 's|#include "lantana/backends/deepstream/|#include "engine/pipeline/|g' \
    -e 's|#include "lantana/core/|#include "engine/core/|g'

# Update ds_ file references
find . -name "*.hpp" -o -name "*.cpp" | xargs sed -i \
    -e 's|ds_analytics_builder|analytics_builder|g' \
    -e 's|ds_demuxer_builder|demuxer_builder|g' \
    -e 's|ds_encoder_builder|encoder_builder|g' \
    -e 's|ds_infer_builder|infer_builder|g' \
    -e 's|ds_msgconv_broker_builder|msgconv_broker_builder|g' \
    -e 's|ds_muxer_builder|muxer_builder|g' \
    -e 's|ds_osd_builder|osd_builder|g' \
    -e 's|ds_queue_builder|queue_builder|g' \
    -e 's|ds_sink_builder|sink_builder|g' \
    -e 's|ds_smart_record_builder|smart_record_builder|g' \
    -e 's|ds_source_builder|source_builder|g' \
    -e 's|ds_tiler_builder|tiler_builder|g' \
    -e 's|ds_tracker_builder|tracker_builder|g' \
    -e 's|ds_builder_factory|builder_factory|g' \
    -e 's|ds_pipeline_manager|pipeline_manager|g' \
    -e 's|ds_runtime_stream_manager|runtime_stream_manager|g' \
    -e 's|ds_smart_record_controller|smart_record_controller|g' \
    -e 's|ds_config_validator|config_validator|g' \
    -e 's|ds_smuxer_builder|smuxer_builder|g'

# Update v2 references to v1 name (after deleting v1)
find . -name "*.hpp" -o -name "*.cpp" | xargs sed -i \
    -e 's|outputs_builder_v2|outputs_builder|g' \
    -e 's|crop_object_handler_v2|crop_object_handler|g' \
    -e 's|smart_record_handler_v2|smart_record_handler|g' \
    -e 's|ext_proc_svc_v2|ext_proc_svc|g'
```

### Rework 4: Class Name Updates (ds_ prefix + _v2 suffix)

In addition to filenames, update class names within code:

```bash
# Drop Ds prefix from class names
find . -name "*.hpp" -o -name "*.cpp" | xargs sed -i \
    -e 's/DsAnalyticsBuilder/AnalyticsBuilder/g' \
    -e 's/DsDemuxerBuilder/DemuxerBuilder/g' \
    -e 's/DsEncoderBuilder/EncoderBuilder/g' \
    -e 's/DsInferBuilder/InferBuilder/g' \
    -e 's/DsMsgconvBrokerBuilder/MsgconvBrokerBuilder/g' \
    -e 's/DsMuxerBuilder/MuxerBuilder/g' \
    -e 's/DsOsdBuilder/OsdBuilder/g' \
    -e 's/DsQueueBuilder/QueueBuilder/g' \
    -e 's/DsSinkBuilder/SinkBuilder/g' \
    -e 's/DsSmartRecordBuilder/SmartRecordBuilder/g' \
    -e 's/DsSourceBuilder/SourceBuilder/g' \
    -e 's/DsTilerBuilder/TilerBuilder/g' \
    -e 's/DsTrackerBuilder/TrackerBuilder/g' \
    -e 's/DsBuilderFactory/BuilderFactory/g' \
    -e 's/DsPipelineManager/PipelineManager/g' \
    -e 's/DsRuntimeStreamManager/RuntimeStreamManager/g' \
    -e 's/DsSmartRecordController/SmartRecordController/g' \
    -e 's/DsConfigValidator/ConfigValidator/g'

# Drop _v2 suffix from class names
find . -name "*.hpp" -o -name "*.cpp" | xargs sed -i \
    -e 's/OutputsBuilderV2/OutputsBuilder/g' \
    -e 's/CropObjectHandlerV2/CropObjectHandler/g' \
    -e 's/SmartRecordHandlerV2/SmartRecordHandler/g' \
    -e 's/ExtProcSvcV2/ExtProcSvc/g'
```

### Rework 5: Full Config Pattern — Builders Receive `PipelineConfig` Directly

**lantanav2 pattern (slice-then-pass):**
```cpp
// PipelineBuilder extracts slice trước rồi mới pass xuống
class SourceBuilder {
    void build(const std::vector<SourceConfig>& sources) { ... }  // Chỉ nhận slice
};

class InferBuilder {
    void build(const InferenceConfig& infer_cfg) { ... }         // Chỉ nhận slice
};

// Caller phải extract thủ công
builder_factory->get_source_builder()->build(config.sources);
builder_factory->get_infer_builder()->build(config.processing_flow[i]);
```

**vms-engine pattern (full config xuyên suốt):**
```cpp
// Mọi builder đều nhận full config — truy cập bất cứ section nào cần
class SourceBuilder {
    void build(const engine::core::config::PipelineConfig& config) {
        for (const auto& src : config.sources) {
            // Có thể check config.stream_muxer->batch_size ngay đây nếu cần
        }
    }
};

class InferBuilder {
    void build(const engine::core::config::PipelineConfig& config, int index) {
        const auto& infer = config.processing_flow[index];
        // Cross-reference: check config.stream_muxer->batch_size
        // Cross-reference: check config.sources.size() để tính batch
        // Tất cả available — không cần ai pass thêm gì
    }
};

class OutputsBuilder {
    void build(const engine::core::config::PipelineConfig& config) {
        // Check config.smart_record trực tiếp — không cần được pass từ ngoài
        if (config.smart_record.has_value() && config.smart_record->enable) {
            // wire smart record branch
        }
        for (const auto& output : config.outputs) { ... }
    }
};
```

**Lợi ích:**
- Builders có thể **cross-reference** bất kỳ section nào của config
- Không có boilerplate extract-and-pass ở caller
- `PipelineConfig` là `const&` — read-only, không có side effects
- Khi thêm config section mới không cần thay đổi interface của tất cả builders

**Áp dụng cho tất cả:**
- Tất cả `block_builders/*` — signature: `void build(const PipelineConfig& config)`
- Tất cả `builders/*_builder` — signature: `void build(const PipelineConfig& config, int index = 0)`
- `IElementBuilder` interface trong `core/builders/ielement_builder.hpp` cũng cập nhật theo (xem Plan 02)

### Rework 6: Apply RAII to All Builder Error Paths

All element builders in lantanav2 create `GstElement*` via `gst_element_factory_make()` and may
return early before `gst_bin_add()`. Without RAII, these early returns silently leak memory.

**Pattern to apply in every builder `.cpp`:**

```cpp
#include "engine/core/utils/gst_utils.hpp"  // add this include

GstElement* InferBuilder::build(const engine::core::config::PipelineConfig& config, int index) {
    const auto& elem_cfg = config.processing.elements[index];

    // ✅ RAII guard: auto-unref on any early return before gst_bin_add()
    auto elem = engine::core::utils::make_gst_element("nvinfer", elem_cfg.id.c_str());
    if (!elem) {
        LOG_E("Failed to create nvinfer '{}'", elem_cfg.id);
        return nullptr;  // ~GstElementPtr → gst_object_unref automatically
    }

    g_object_set(G_OBJECT(elem.get()),
        "config-file-path", elem_cfg.config_file_path.c_str(),
        nullptr);

    if (!gst_bin_add(GST_BIN(bin_), elem.get())) {
        LOG_E("Failed to add nvinfer '{}' to bin", elem_cfg.id);
        return nullptr;  // guard cleans up
    }
    return elem.release();  // bin owns — disarm guard
}
```

**Pad access in probes and handlers:**

```cpp
// ✅ Always wrap: gst_element_get_static_pad increments refcount
void MyHandler::register_callback() {
    engine::core::utils::GstPadPtr pad(
        gst_element_get_static_pad(element_, "sink"), gst_object_unref);
    if (!pad) { LOG_E("Pad not found"); return; }
    gst_pad_add_probe(pad.get(), GST_PAD_PROBE_TYPE_BUFFER, my_cb, this, nullptr);
}  // gst_object_unref(pad) at end of scope automatically
```

**Multi-step cleanup — use a class, not just unique_ptr:**

```cpp
// PipelineManager holds the top-level GstPipeline — must set NULL before unref
class GstPipelineOwner {
public:
    explicit GstPipelineOwner(const std::string& name)
        : pipeline_(gst_pipeline_new(name.c_str())) {}
    ~GstPipelineOwner() {
        if (pipeline_) {
            gst_element_set_state(pipeline_, GST_STATE_NULL);  // drain first
            gst_object_unref(pipeline_);
        }
    }
    GstPipelineOwner(const GstPipelineOwner&)            = delete;
    GstPipelineOwner& operator=(const GstPipelineOwner&) = delete;
    GstElement* get() const { return pipeline_; }
private:
    GstElement* pipeline_ = nullptr;
};
```

**Files to review and update:**
- All `pipeline/src/builders/*.cpp` — wrap `gst_element_factory_make()` return
- All `pipeline/src/block_builders/*.cpp` — same
- `pipeline/src/pipeline_manager.cpp` — wrap bus and top-level pipeline
- `pipeline/src/event_handlers/*.cpp` — wrap `gst_element_get_static_pad()` returns
- `pipeline/src/probes/*.cpp` — wrap `gst_element_get_static_pad()` returns

> Reference: [Memory Management section in ARCHITECTURE_BLUEPRINT.md](../../architecture/ARCHITECTURE_BLUEPRINT.md#memory-management)

---

## Namespace Replacement

```bash
cd vms-engine/pipeline

# Replace namespace declarations
find . -name "*.hpp" -o -name "*.cpp" | xargs sed -i \
    -e 's/namespace lantana::backends::deepstream/namespace engine::pipeline/g' \
    -e 's/namespace lantana {/namespace engine {/g' \
    -e 's/lantana::backends::deepstream::/engine::pipeline::/g' \
    -e 's/lantana::/engine::/g'

# Replace include guards
find . -name "*.hpp" | xargs sed -i \
    -e 's/LANTANA_BACKENDS_DEEPSTREAM_/ENGINE_PIPELINE_/g' \
    -e 's/LANTANA_/ENGINE_/g'
```

---

## CMakeLists.txt

```cmake
# pipeline/CMakeLists.txt
file(GLOB_RECURSE PIPELINE_SOURCES "src/*.cpp")

add_library(vms_engine_pipeline STATIC ${PIPELINE_SOURCES})

target_include_directories(vms_engine_pipeline
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PUBLIC ${DEEPSTREAM_DIR}/sources/includes
)

target_link_libraries(vms_engine_pipeline
    PUBLIC  vms_engine_core
    PUBLIC  PkgConfig::GST
    PUBLIC  PkgConfig::GST_BASE
    PUBLIC  PkgConfig::GST_VIDEO
    PUBLIC  PkgConfig::GST_APP
    PUBLIC  PkgConfig::GST_RTSP
    PUBLIC  PkgConfig::GLIB2
    PUBLIC  CUDA::cudart
    PRIVATE ${NVDS_LIBS}    # nvds_meta, nvdsgst_meta, etc.
    PRIVATE nlohmann_json::nlohmann_json
    PRIVATE yaml-cpp
)
```

---

## File Count Summary

| Category            | Headers | Sources | Deleted (v1) | Net Files |
| -------------------- | ------- | ------- | ------------ | ---------- |
| Root managers        | 6       | 4       | 0            | 10         |
| Block builders       | 6       | 2       | 0            | 8          |
| Outputs builder      | 5       | 0       | 1 (v1)       | 5          |
| Element builders     | 13      | 16      | 0            | 29         |
| Config               | 1       | 1       | 0            | 2          |
| Event handlers       | 4       | 4       | 2 (v1 h+cpp) | 8          |
| Linking              | 1       | 1       | 0            | 2          |
| Probes               | 4       | 4       | 2 (v1 h+cpp) | 8          |
| Services             | 1       | 1       | 2 (v1 h+cpp) | 2          |
| Utils                | 1       | 1       | 0            | 2          |
| **Total**            | **42**  | **34**  | **7**        | **76 → 69**|

---

## Verification

```bash
# 1. Compile pipeline library
cmake --build build --target vms_engine_pipeline -- -j$(nproc)

# 2. Check no old references
grep -r "lantana\|backends/deepstream\|ds_.*builder\|_v2" pipeline/ && echo "FAIL" || echo "PASS"

# 3. Check no #ifdef guards for backends
grep -r "LANTANA_WITH_DEEPSTREAM\|LANTANA_WITH_DLSTREAMER" pipeline/ && echo "FAIL" || echo "PASS"

# 4. Verify all headers use engine::pipeline namespace
grep -rL "engine::pipeline" pipeline/include/ | head -20
```

---

## Checklist

- [ ] 42 headers → `pipeline/include/engine/pipeline/`
- [ ] 34 source files → `pipeline/src/`
- [ ] 7 v1 files deleted (smart_record_handler, crop_object_handler, ext_proc_svc, outputs_builder v1)
- [ ] v2 files renamed to drop `_v2` suffix
- [ ] `ds_` prefix removed from all filenames
- [ ] `Ds`/`DS` prefix removed from all class names
- [ ] `lantana::backends::deepstream::` → `engine::pipeline::`
- [ ] Include paths updated to `engine/pipeline/`
- [ ] Include guards updated to `ENGINE_PIPELINE_*`
- [ ] All `#ifdef LANTANA_WITH_*` guards removed
- [ ] Event handlers use injected `IMessageProducer*` (no static Redis)
- [ ] All builders use `build(const PipelineConfig& config, ...)` — full config pattern (no sliced pass)
- [ ] `IElementBuilder` interface updated in core (see Plan 02)
- [ ] **RAII applied to all builders** — `make_gst_element()` + `elem.release()` after `gst_bin_add()`
- [ ] **RAII applied to all pad accesses** — `GstPadPtr` wraps `gst_element_get_static_pad()`
- [ ] **`GstPipelineOwner` class** in `PipelineManager` — `set_state(NULL)` before unref
- [ ] `pipeline/CMakeLists.txt` updated
- [ ] `vms_engine_pipeline` compiles against `vms_engine_core`
