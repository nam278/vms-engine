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

} else if (cfg.trigger == "class_id_offset") {
    auto* handler = new ClassIdNamespaceHandler();
    handler->configure(full_config_, ClassIdNamespaceHandler::Mode::Offset, elem_index);
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER,
        ClassIdNamespaceHandler::on_buffer, handler,
        [](gpointer ud) { delete static_cast<ClassIdNamespaceHandler*>(ud); });

} else if (cfg.trigger == "class_id_restore") {
    auto* handler = new ClassIdNamespaceHandler();
    handler->configure(full_config_, ClassIdNamespaceHandler::Mode::Restore);
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER,
        ClassIdNamespaceHandler::on_buffer, handler,
        [](gpointer ud) { delete static_cast<ClassIdNamespaceHandler*>(ud); });
}
```

Ownership của handler instance được transfer sang GStreamer qua `GDestroyNotify` callback — không cần lifecycle management thêm.

> **`pad_name` field**: `ProbeHandlerManager` đọc `cfg.pad_name` (default `"src"`) để lấy đúng pad. `class_id_offset` cần `pad_name: sink` vì phải chạy **trước** `nvtracker` xử lý metadata.

---

## 3. Built-in Probe Handlers

### 3.1 SmartRecordProbeHandler

Trigger smart recording khi phát hiện object khớp `label_filter`. Phát `start-sr` GSignal trực tiếp trên `nvurisrcbin` bên trong `nvmultiurisrcbin`.

```cpp
// pipeline/include/engine/pipeline/probes/smart_record_probe_handler.hpp
struct RecordingSession {
    uint32_t session_id = 0;
    GstClockTime start_time = GST_CLOCK_TIME_NONE;  // gst_system_clock time
    uint32_t duration_sec = 0;
    // ...
};

struct SourceRecordingState {
    GstClockTime last_record_time = GST_CLOCK_TIME_NONE; // actual_start (pre-adjusted)
    std::optional<RecordingSession> active_session;
    gulong signal_handler_id = 0;
};

class SmartRecordProbeHandler {
public:
    void configure(const engine::core::config::PipelineConfig& config,
                   const engine::core::config::EventHandlerConfig& handler,
                   GstElement* multiuribin,
                   engine::core::messaging::IMessageProducer* producer);

    static GstPadProbeReturn on_buffer(GstPad* pad,
                                       GstPadProbeInfo* info,
                                       gpointer user_data);
private:
    // timing
    bool can_start_recording(uint32_t source_id, GstClockTime now);
    void start_recording(uint32_t source_id, uint64_t trigger_obj_id, GstClockTime now);
    void cleanup_expired_sessions(GstClockTime now);   // clear sessions missing sr-done
    int  count_active_recordings() const;

    // element discovery (+ stale-cache eviction)
    GstElement* find_nvurisrcbin(uint32_t source_id);

    int max_concurrent_recordings_ = 0;  // 0 = unlimited
    std::atomic<bool> shutting_down_{false};
    std::unordered_map<uint32_t, SourceRecordingState> source_states_;
    // ...
};
```

**Cải tiến so với v1:**

| Feature          | v1                          | v2 (hiện tại)                                 |
| ---------------- | --------------------------- | --------------------------------------------- |
| Timing           | `std::chrono::steady_clock` | `GstClockTime` (gst_system_clock)             |
| Interval ref     | detection time              | `actual_start = now - pre_event_ns`           |
| Stale cache      | không kiểm tra              | evict nếu element không còn trong bin         |
| Max concurrent   | không có                    | `max_concurrent_recordings` config field      |
| Expired sessions | không có                    | `cleanup_expired_sessions()` với grace period |
| Teardown safety  | không có                    | `shutting_down_` atomic flag                  |

**Flow inside `on_buffer`:**

1. Check `shutting_down_` → trả về sớm nếu đang tắt
2. `get_gst_clock_now()` → lấy `GstClockTime now` một lần cho cả batch
3. `cleanup_expired_sessions(now)` → dọn sessions thiếu sr-done
4. `gst_buffer_get_nvds_batch_meta(buf)` → batch metadata
5. Iterate frame → object metadata; check `label_filter_`
6. `can_start_recording(source_id, now)` → kiểm tra active_session + min_interval + max_concurrent
7. On match → `start_recording(source_id, object_id, now)`
8. `on_recording_done` callback (sr-done signal) → clear active_session, publish `record_done`

> 📖 **Chi tiết đầy đủ**: [`docs/architecture/probes/smart_record_probe_handler.md`](../probes/smart_record_probe_handler.md)

### 3.2 CropObjectHandler

Crop detected objects từ GPU frame thành ảnh JPEG sử dụng **NvDsObjEnc** CUDA-accelerated encoder. Publish metadata JSON đến Redis Streams.

```cpp
// pipeline/include/engine/pipeline/probes/crop_object_handler.hpp
enum class PubDecisionType { None, FirstSeen, Heartbeat };

struct ObjectPubState {
    GstClockTime last_publish_pts = GST_CLOCK_TIME_NONE;
    uint64_t heartbeat_seq = 0;
    std::size_t last_payload_hash = 0;
    std::string last_message_id;
};

class CropObjectHandler {
public:
    void configure(const engine::core::config::PipelineConfig& config,
                   const engine::core::config::EventHandlerConfig& handler,
                   engine::core::messaging::IMessageProducer* producer);

    static GstPadProbeReturn on_buffer(GstPad* pad,
                                       GstPadProbeInfo* info,
                                       gpointer user_data);
private:
    PubDecisionType decide_capture(uint64_t key, GstClockTime pts);
    std::size_t compute_payload_hash(int class_id, const std::string& label,
                                     float left, float top, float w, float h);
    void publish_pending_messages();

    std::unordered_map<uint64_t, std::string> object_keys_;        // persistent UUIDv7
    std::unordered_map<uint64_t, GstClockTime> object_last_seen_;  // stale detection
    std::unordered_map<uint64_t, GstClockTime> last_capture_pts_;  // PTS throttle
    std::unordered_map<uint64_t, ObjectPubState> pub_state_;       // dedup + chain
};
```

**Cải tiến so với v1:**

| Feature             | v1                      | v2 (hiện tại)                                                |
| ------------------- | ----------------------- | ------------------------------------------------------------ |
| Publish decision    | Capture = publish       | `PubDecisionType` enum (FirstSeen/Heartbeat/None)            |
| Heartbeat dedup     | Không có                | Payload hash — suppress duplicate heartbeat                  |
| Publish timing      | Publish ngay khi encode | Batch-accumulate → `nvds_obj_enc_finish` → publish           |
| Message ID chain    | Không có                | `mid` + `prev_mid` cho event correlation                     |
| Stale cleanup scope | 3 maps                  | 4 maps (thêm `pub_state_`)                                   |
| Memory stats        | Không có                | `log_memory_stats()` mỗi cleanup cycle                       |
| Emergency limit     | 10000                   | 5000 (stricter)                                              |
| File naming         | `crop_{tid}_{uuid8}`    | `crop_{label}_{tid}_{uuid8}` (sanitized label)               |
| ext_processor       | Không parse             | Parsed vào `ExtProcessorConfig` → `ExternalProcessorService` |

**Batch-accumulate flow:**

1. `cudaDeviceSynchronize()` — đảm bảo GPU buffer sẵn sàng
2. Iterate frame → object → `decide_capture()` → `compute_payload_hash()`
3. `nvds_obj_enc_process()` — queue CUDA encode cho mỗi object
4. Accumulate `PendingMessage` vào vector
5. `ext_proc_svc_->process_object()` — (nếu có rule) encode in-memory JPEG + launch detached HTTP call
6. `nvds_obj_enc_finish()` — block cho đến khi TẤT CẢ JPEG ghi xong
7. `publish_pending_messages()` — publish JSON đến broker

> 📖 **Chi tiết đầy đủ**: [`docs/architecture/probes/crop_object_handler.md`](../probes/crop_object_handler.md)

### 3.3 ExternalProcessorService

Service tích hợp bên trong `CropObjectHandler`, thực hiện **HTTP-based AI enrichment** cho từng detected object (face recognition, license plate lookup…).

- Mỗi object có matching label rule → encode in-memory JPEG (engine context riêng, `saveImg=FALSE`) → HTTP multipart POST → parse JSON response → publish `event: ext_proc` đến Redis channel.
- API call chạy trong **detached thread** — không block GStreamer streaming thread.
- Throttle per `(source_id, tracker_id, label)` để tránh flood endpoint cùng object.
- Cấu hình sub-block `ext_processor:` bên trong `event_handlers` entry có `trigger: crop_objects`.

> 📖 **Chi tiết đầy đủ**: [`docs/architecture/probes/ext_proc_svc.md`](../probes/ext_proc_svc.md)

### 3.4 ClassIdNamespaceHandler

Giải quyết **class_id collision** trong multi-detector pipelines (PGIE + nhiều SGIE).

```cpp
// pipeline/include/engine/pipeline/probes/class_id_namespace_handler.hpp
class ClassIdNamespaceHandler {
public:
    enum class Mode { Offset, Restore };

    void configure(const engine::core::config::PipelineConfig& config, Mode mode,
                   int element_index = -1);

    void set_explicit_offsets(const std::unordered_map<int, int>& offsets);

    static GstPadProbeReturn on_buffer(GstPad* pad,
                                       GstPadProbeInfo* info,
                                       gpointer user_data);
};
```

Hai chế độ:

| Mode      | Trigger YAML       | Pad    | Mục đích                                             |
| --------- | ------------------ | ------ | ---------------------------------------------------- |
| `Offset`  | `class_id_offset`  | `sink` | Remap `class_id → (gie_unique_id × 1000) + class_id` |
| `Restore` | `class_id_restore` | `src`  | Phục hồi `class_id` gốc từ `misc_obj_info[]`         |

Giá trị gốc được lưu trong `misc_obj_info[0..2]` của `NvDsObjectMeta` với magic marker `0x4C4E5441` ("LNTA").

> 📖 **Chi tiết đầy đủ**: [`docs/architecture/probes/class_id_namespacing_handler.md`](../probes/class_id_namespacing_handler.md)

---

## 4. EventHandlerConfig — YAML Schema

```yaml
# ── PROBE ORDERING NOTE (GStreamer FIFO) ─────────────────────────────────────
# Probes trên cùng một pad được thực thi theo thứ tự đăng ký.
# class_id_restore PHẢI đứng TRƯỚC smart_record/crop_objects trong list này.
# ─────────────────────────────────────────────────────────────────────────────
event_handlers:
  - id: class_id_offset # MUST be first — runs on tracker's SINK pad
    enable: false
    type: on_detect
    probe_element: tracker
    pad_name: sink # ← attach to input pad (before nvtracker)
    trigger: class_id_offset

  - id: class_id_restore # MUST be before smart_record/crop_objects
    enable: false
    type: on_detect
    probe_element: tracker
    pad_name: src
    trigger: class_id_restore

  - id: smart_record
    enable: true
    type: on_detect
    probe_element: tracker # element to attach probe to
    source_element: nvmultiurisrcbin0 # nvmultiurisrcbin element name
    trigger: smart_record # pad_name defaults to "src"
    label_filter:
      - car
      - person
      - truck
    pre_event_sec: 2 # seconds of pre-event buffer
    post_event_sec: 20 # seconds to record after trigger
    min_interval_sec: 30 # minimum seconds between recordings per source
    max_concurrent_recordings: 2 # 0 = unlimited
    broker:
      host: localhost
      port: 6379
      channel: vms:events:smart_record

  - id: crop_objects
    enable: true
    type: on_detect
    probe_element: tracker
    trigger: crop_objects
    label_filter: [car, person, truck]
    save_dir: "/opt/engine/data/rec/objects"
    capture_interval_sec: 5
    image_quality: 85
    save_full_frame: true
    cleanup:
      stale_object_timeout_min: 5
      check_interval_batches: 30
      old_dirs_max_days: 7
    broker:
      host: 192.168.1.99
      port: 6319
      channel: worker_lsr_snap
    ext_processor:
      enable: false
      min_interval_sec: 1
      rules:
        - label: face
          endpoint: http://localhost:8000/api/v1/face/recognize
          result_path: match.external_id
          display_path: match.face_name
          params:
            threshold: "0.65"
```

**Field reference:**

| Field                              | Type     | Required | Handler      | Notes                                                                            |
| ---------------------------------- | -------- | -------- | ------------ | -------------------------------------------------------------------------------- |
| `id`                               | string   | ✅       | all          | Unique across all handlers                                                       |
| `enable`                           | bool     | ✅       | all          | false = handler skipped entirely                                                 |
| `type`                             | string   | ✅       | all          | Event category, e.g. `on_detect`                                                 |
| `probe_element`                    | string   | ✅       | all          | Element name in pipeline to attach probe to                                      |
| `pad_name`                         | string   | optional | all          | `"src"` (default) or `"sink"`                                                    |
| `trigger`                          | string   | ✅       | all          | `"smart_record"` / `"crop_objects"` / `"class_id_offset"` / `"class_id_restore"` |
| `label_filter`                     | string[] | optional | SR, crop     | Empty = all labels match                                                         |
| `source_element`                   | string   | ✅ SR    | smart_record | Name of `nvmultiurisrcbin` element                                               |
| `pre_event_sec`                    | int      | optional | smart_record | Pre-event buffer seconds (default: 2)                                            |
| `post_event_sec`                   | int      | optional | smart_record | Post-event record seconds (default: 20)                                          |
| `min_interval_sec`                 | int      | optional | smart_record | Min seconds between recordings per source (default: 2)                           |
| `max_concurrent_recordings`        | int      | optional | smart_record | Max simultaneous recordings, 0 = unlimited (default: 0)                          |
| `save_dir`                         | string   | optional | crop_objects | Output dir for crop images                                                       |
| `capture_interval_sec`             | int      | optional | crop_objects | PTS-based throttle per object (default: 5s)                                      |
| `image_quality`                    | int      | optional | crop_objects | JPEG quality 1–100 (default: 85)                                                 |
| `save_full_frame`                  | bool     | optional | crop_objects | `true` = save full-frame alongside crop (default: true)                          |
| `cleanup.stale_object_timeout_min` | int      | optional | crop_objects | Remove object state after N minutes unseen (default: 5)                          |
| `cleanup.check_interval_batches`   | int      | optional | crop_objects | Run cleanup every N batches (default: 30)                                        |
| `cleanup.old_dirs_max_days`        | int      | optional | crop_objects | Delete daily dirs older than N days, 0=off (default: 7)                          |
| `broker.host`                      | string   | optional | SR, crop     | Redis host for event publishing                                                  |
| `broker.port`                      | int      | optional | SR, crop     | Redis port (default: 6379)                                                       |
| `broker.channel`                   | string   | optional | SR, crop     | Redis channel/key to publish JSON events                                         |
| `ext_processor.enable`             | bool     | optional | crop_objects | Enable external processor (default: false)                                       |
| `ext_processor.min_interval_sec`   | int      | optional | crop_objects | Min seconds between ext processor calls (default: 1)                             |
| `ext_processor.rules[]`            | object[] | optional | crop_objects | Rules: label, endpoint, result_path, display_path, params map                    |

### Probe Ordering — GStreamer FIFO

GStreamer thực thi probes trên cùng một pad theo **thứ tự đăng ký (FIFO)**, là thứ tự entries trong `event_handlers:`. Điều này ảnh hưởng trực tiếp khi dùng `class_id_restore` kết hợp với `smart_record`/`crop_objects`:

```
tracker:src pad (thứ tự FIFO):
  1. class_id_restore   ← PHẢI đứng ĐẦU — phục hồi class_ids gốc
  2. smart_record        ← thấy class_ids đã restore → label_filter đúng ✅
  3. crop_objects        ← thấy class_ids đã restore → label_filter đúng ✅
```

> ⚠️ Nếu `class_id_restore` đứng sau `smart_record`/`crop_objects`, các handler sẽ thấy class_ids bị offset → `label_filter` KHÔNG khớp.

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
