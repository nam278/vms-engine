# 07. Pad Probes & Event Handlers

## 1. Tổng quan

vms-engine dùng **GStreamer pad probes** để can thiệp vào pipeline buffer stream. Đây là cơ chế duy nhất cho event handling — signal-based handlers (`g_signal_connect`) đã bị loại bỏ.

| Cơ chế        | API                   | Dùng khi nào                                                 |
| ------------- | --------------------- | ------------------------------------------------------------ |
| **Pad Probe** | `gst_pad_add_probe()` | Inspect/modify NvDs metadata ở buffer level trên element pad |

Pad probe chạy **trực tiếp trên pipeline streaming thread** — callback phải nhanh, không block I/O, không lock mutex lâu.

## 2. ProbeHandlerManager

`ProbeHandlerManager` là coordinator đọc `EventHandlerConfig` từ YAML, tạo probe handler phù hợp với `trigger`, và attach lên `probe_element`'s src pad.

```cpp
// pipeline/include/engine/pipeline/probes/probe_handler_manager.hpp
namespace engine::pipeline::probes {

class ProbeHandlerManager {
public:
    explicit ProbeHandlerManager(GstElement* pipeline);

    /// Attach probes từ event_handler configs.
    bool attach_probes(const std::vector<engine::core::config::EventHandlerConfig>& configs);

    /// Remove tất cả probes (gọi trước pipeline teardown).
    void detach_all();
};
}
```

### Dispatch logic

`attach_probes()` nhìn vào `cfg.trigger` để tạo đúng handler:

```cpp
if (cfg.trigger == "smart_record") {
    auto* handler = new SmartRecordProbeHandler();
    handler->configure(cfg);
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER,
        SmartRecordProbeHandler::on_buffer, handler,
        [](gpointer ud) { delete static_cast<SmartRecordProbeHandler*>(ud); });

} else if (cfg.trigger == "crop_objects") {
    auto* handler = new CropObjectHandler();
    handler->configure(cfg);
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER,
        CropObjectHandler::on_buffer, handler,
        [](gpointer ud) { delete static_cast<CropObjectHandler*>(ud); });
}
```

Ownership của handler instance được transfer sang GStreamer qua `GDestroyNotify` callback — không cần lifecycle management thêm.

---

## 3. Built-in Probe Handlers

### 3.1 SmartRecordProbeHandler

Trigger smart recording khi phát hiện object khớp `label_filter`.

```cpp
// pipeline/include/engine/pipeline/probes/smart_record_probe_handler.hpp
class SmartRecordProbeHandler {
public:
    void configure(const engine::core::config::EventHandlerConfig& config);

    static GstPadProbeReturn on_buffer(GstPad* pad,
                                       GstPadProbeInfo* info,
                                       gpointer user_data);
private:
    std::vector<std::string> label_filter_;
    int pre_event_sec_ = 2;
    int post_event_sec_ = 20;
    std::string save_dir_;
};
```

**Typical flow inside `on_buffer`:**

1. `gst_buffer_get_nvds_batch_meta(buf)` → get batch metadata
2. Iterate frame → object metadata
3. Check `obj_meta->obj_label` against `label_filter_`
4. On match → call `NvDsSRStart()` directly on the source bin

### 3.2 CropObjectHandler

Crop detected objects từ GPU frame và lưu JPEG.

```cpp
// pipeline/include/engine/pipeline/probes/crop_object_handler.hpp
class CropObjectHandler {
public:
    void configure(const engine::core::config::EventHandlerConfig& config);

    static GstPadProbeReturn on_buffer(GstPad* pad,
                                       GstPadProbeInfo* info,
                                       gpointer user_data);
private:
    std::vector<std::string> label_filter_;
    std::string save_dir_;
    int capture_interval_sec_ = 5;
    int image_quality_ = 85;
    bool save_full_frame_ = true;
    std::unordered_map<int, int64_t> last_capture_time_;  // throttle per source
};
```

**Typical flow:**

1. Map NvBufSurface via `NvBufSurfaceMap()`
2. Extract ROI from `obj_meta->rect_params`
3. Encode to JPEG, save to `save_dir_/source_{id}/frame_{num}_{obj_id}.jpg`
4. `NvBufSurfaceUnMap()`

---

## 4. EventHandlerConfig — YAML Schema

```yaml
event_handlers:
  - id: smart_record # Unique identifier
    enable: true # false = skip entirely
    type: on_detect # Event category (informational)
    probe_element: tracker # GstElement name to attach probe to
    trigger: smart_record # Handler type: "smart_record" | "crop_objects"
    label_filter: # Object labels that activate this handler
      - car
      - person
      - truck

  - id: crop_objects
    enable: true
    type: on_detect
    probe_element: tracker
    trigger: crop_objects
    label_filter: [car, person]
    save_dir: "/opt/engine/data/rec/objects"
    capture_interval_sec: 10
    image_quality: 85
    save_full_frame: false
```

**Field reference:**

| Field                  | Type     | Required | Notes                                            |
| ---------------------- | -------- | -------- | ------------------------------------------------ |
| `id`                   | string   | ✅       | Unique across all handlers                       |
| `enable`               | bool     | ✅       | false = handler skipped entirely                 |
| `type`                 | string   | ✅       | Event category, e.g. `on_detect`                 |
| `probe_element`        | string   | ✅       | Element name in pipeline to attach probe to      |
| `trigger`              | string   | ✅       | `"smart_record"` or `"crop_objects"`             |
| `label_filter`         | string[] | optional | Empty = all labels match                         |
| `save_dir`             | string   | optional | Output dir for recorded files / crop images      |
| `capture_interval_sec` | int      | optional | Crop throttle (default: 5s)                      |
| `image_quality`        | int      | optional | JPEG quality 1–100 (default: 85)                 |
| `save_full_frame`      | bool     | optional | `true` = save whole frame instead of object crop |

---

## 5. Adding a New Probe Handler

1. **Header** — `pipeline/include/engine/pipeline/probes/my_probe_handler.hpp`

```cpp
#pragma once
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>

namespace engine::pipeline::probes {

/**
 * @brief Pad probe handler for <task description>.
 */
class MyProbeHandler {
public:
    /**
     * @brief Configure handler from EventHandlerConfig.
     * @param config  Parsed YAML event_handlers entry.
     */
    void configure(const engine::core::config::EventHandlerConfig& config);

    /**
     * @brief GStreamer pad probe callback.
     * Must be fast — runs on pipeline streaming thread.
     */
    static GstPadProbeReturn on_buffer(GstPad* pad,
                                       GstPadProbeInfo* info,
                                       gpointer user_data);
private:
    std::vector<std::string> label_filter_;
    std::string save_dir_;
};

}  // namespace engine::pipeline::probes
```

2. **Implementation** — `pipeline/src/probes/my_probe_handler.cpp`

3. **Register in ProbeHandlerManager** — add `else if (cfg.trigger == "my_trigger")` block in `attach_probes()` in `pipeline/src/probes/probe_handler_manager.cpp`

4. **YAML** — add entry under `event_handlers:` with `trigger: my_trigger`

---

## 6. Probe Callback Rules

```cpp
static GstPadProbeReturn on_buffer(GstPad*, GstPadProbeInfo* info, gpointer user_data) {
    auto* self = static_cast<MyProbeHandler*>(user_data);
    GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
    NvDsBatchMeta* meta = gst_buffer_get_nvds_batch_meta(buf);
    if (!meta) return GST_PAD_PROBE_OK;

    for (NvDsMetaList* fl = meta->frame_meta_list; fl; fl = fl->next) {
        auto* frame = static_cast<NvDsFrameMeta*>(fl->data);
        // process frame...
    }

    return GST_PAD_PROBE_OK;  // ✅ Always return OK unless intentionally dropping
}
```

**Rules:**

- ✅ Return `GST_PAD_PROBE_OK` unless you're intentionally dropping the buffer
- ✅ Use `GST_PAD_PROBE_INFO_BUFFER(info)` to get the buffer — do NOT take ownership
- ✅ `NvDsBatchMeta`, `NvDsFrameMeta`, `NvDsObjectMeta` — **DO NOT FREE** these
- ❌ No blocking I/O (file write, HTTP, mutex wait) on the probe callback directly — offload to a worker thread
- ❌ Do not call `gst_buffer_ref()` on the probe buffer without matching `gst_buffer_unref()`
