# Class ID Namespace Handler

## 1. Vấn đề — Class ID Collision trong Multi-Detector Pipelines

Khi pipeline sử dụng **nhiều nvinfer** (PGIE + nhiều SGIE), mỗi detector dùng `class_id` riêng bắt đầu từ `0`. Ví dụ:

| GIE              | `unique_component_id` | Class 0  | Class 1 |
| ---------------- | --------------------- | -------- | ------- |
| PGIE (detection) | 1                     | person   | car     |
| SGIE (face)      | 2                     | identity | emotion |
| SGIE (vehicle)   | 3                     | sedan    | truck   |

Sau khi `nvtracker` tổng hợp metadata từ nhiều GIE, tất cả object đều nằm chung trong cùng một `obj_meta_list`. Lúc đó:

- `smart_record` dùng `label_filter` để match — label lookup dựa vào `class_id` → **sai label** nếu SGIE class 0 trùng với PGIE class 0.
- `crop_objects` cũng đọc `class_id` để filter → tương tự lỗi.
- Downstream analytics (nvdsanalytics) cần phân biệt classes across GIEs.

**Giải pháp**: Offset `class_id` của mỗi GIE bằng một lượng dựa trên `unique_component_id` — trước khi dữ liệu vào tracker. Sau tracker, restore về giá trị gốc khi cần.

---

## 2. Kiến trúc — Hai Probe, Hai Pad

```
[nvinfer PGIE] --(src pad)--> ... --> [nvtracker] --(src pad)--> [OSD / smart_record / crop_objects]
                                             ↑                          ↑
                                      class_id_offset              class_id_restore
                                      (sink pad, BEFORE tracker)   (src pad, AFTER tracker)
```

| Handler            | Pad              | Thời điểm     | Mục đích                                     |
| ------------------ | ---------------- | ------------- | -------------------------------------------- |
| `class_id_offset`  | `tracker` — sink | Trước tracker | Remap `class_id` → `base_offset + class_id`  |
| `class_id_restore` | `tracker` — src  | Sau tracker   | Phục hồi `class_id` gốc từ `misc_obj_info[]` |

---

## 3. Offset Formula

```
base_offset = gie_unique_id × offset_step
```

Với `offset_step = 1000` (default):

| GIE ID | base_offset | Class 0 sau offset | Class 1 sau offset |
| ------ | ----------- | ------------------ | ------------------ |
| 1      | 1000        | 1000               | 1001               |
| 2      | 2000        | 2000               | 2001               |
| 3      | 3000        | 3000               | 3001               |

Sau offset, tất cả class IDs là **globally unique** trong batch — không còn collision.

### Explicit Overrides

Nếu muốn offset tuỳ ý thay vì dùng công thức, gọi:

```cpp
handler.set_explicit_offsets({
    {1, 0},    // PGIE không offset (giữ 0..N)
    {2, 100},  // SGIE face: 100..199
    {3, 200},  // SGIE vehicle: 200..299
});
```

---

## 4. Lưu Trữ Giá Trị Gốc — `misc_obj_info[]`

`NvDsObjectMeta::misc_obj_info` là mảng `gint64[4]` dự phòng cho user data. Handler dùng 3 slot:

| Index | Giá trị                        | Mô tả                        |
| ----- | ------------------------------ | ---------------------------- |
| `[0]` | `0x4C4E5441` = `"LNTA"`        | **Magic marker** — đã offset |
| `[1]` | `original_class_id`            | `class_id` trước khi offset  |
| `[2]` | `original_unique_component_id` | `unique_component_id` gốc    |

**Idempotency**: Probe Offset kiểm tra `misc_obj_info[0] == MAGIC_MARKER` — nếu đã đánh dấu thì **bỏ qua**, không offset lần 2. Điển hình khi pipeline state changes hoặc probe đính kèm nhiều lần.

**Probe Restore** chỉ restore nếu magic marker tồn tại, rồi clear cả 3 slot về 0.

---

## 5. Xếp Thứ Tự Probe — GStreamer FIFO

GStreamer thực thi các probe trên cùng một pad theo **thứ tự đăng ký (FIFO)**. Trong `event_handlers:`, thứ tự entries **quyết định** thứ tự đăng ký:

```
tracker:sink pad:
  1. class_id_offset   ← phải đứng trước tracker (pad: sink)

tracker:src pad (thứ tự đăng ký):
  1. class_id_restore  ← PHẢI đăng ký TRƯỚC smart_record và crop_objects
  2. smart_record      ← thấy class_ids đã restore → label_filter đúng
  3. crop_objects      ← thấy class_ids đã restore → label_filter đúng
```

> ⚠️ **Nếu `class_id_restore` đứng SAU `smart_record`/`crop_objects`** trong config, các handler đó sẽ thấy class_ids đã bị offset → `label_filter` KHÔNG khớp.

---

## 6. YAML Config

```yaml
# ── PROBE ORDERING NOTE ─────────────────────────────────────────────────────
# GStreamer executes probes in FIFO (registration) order.
# class_id_offset  → tracker:sink  (runs before nvtracker processes objects)
# class_id_restore → tracker:src   (MUST register BEFORE smart_record/crop_objects)
# smart_record     → tracker:src   (sees restored IDs → label_filter correct)
# crop_objects     → tracker:src   (sees restored IDs → label_filter correct)
# ────────────────────────────────────────────────────────────────────────────
event_handlers:
  - id: class_id_offset
    enable: false # set true when multi-GIE pipeline is active
    type: on_detect
    probe_element: tracker
    pad_name: sink # IMPORTANT: attach to sink pad (BEFORE nvtracker)
    trigger: class_id_offset
    # label_filter, save_dir, etc. not used by this handler

  - id: class_id_restore
    enable: false
    type: on_detect
    probe_element: tracker
    pad_name: src # explicit (src is also the default)
    trigger: class_id_restore
    # Must appear before smart_record/crop_objects in this list

  - id: smart_record
    enable: true
    type: on_detect
    probe_element: tracker
    trigger: smart_record
    channel: worker_lsr # Redis Stream / Kafka topic
    label_filter: [car, person, truck]

  - id: crop_objects
    enable: true
    type: on_detect
    probe_element: tracker
    trigger: crop_objects
    channel: worker_lsr_snap # Redis Stream / Kafka topic
    label_filter: [car, person]
    save_dir: "/opt/engine/data/rec/objects"
```

### `pad_name` field

| Value    | Pad                | Dùng khi nào                     |
| -------- | ------------------ | -------------------------------- |
| `"src"`  | Element output pad | Default — hầu hết probe handlers |
| `"sink"` | Element input pad  | `class_id_offset` trước tracker  |

Mặc định là `"src"` nếu không khai báo trong YAML.

---

## 7. Code Reference

### File Locations

| File                                                                     | Nội dung                                             |
| ------------------------------------------------------------------------ | ---------------------------------------------------- |
| `pipeline/include/engine/pipeline/probes/class_id_namespace_handler.hpp` | Header — `ClassIdNamespaceHandler`, `Mode` enum      |
| `pipeline/src/probes/class_id_namespace_handler.cpp`                     | Triển khai `process_offset()` / `process_restore()`  |
| `pipeline/src/probes/probe_handler_manager.cpp`                          | Dispatch: `"class_id_offset"` / `"class_id_restore"` |
| `core/include/engine/core/config/config_types.hpp`                       | `EventHandlerConfig::pad_name` field                 |
| `infrastructure/config_parser/src/yaml_parser_handlers.cpp`              | Parse `pad_name` từ YAML                             |

### Class Interface

```cpp
namespace engine::pipeline::probes {

class ClassIdNamespaceHandler {
public:
    enum class Mode { Offset, Restore };

    /**
     * @brief Configure for Offset or Restore mode.
     * @param config         Full pipeline config (for element unique_id lookup).
     * @param mode           Mode::Offset or Mode::Restore.
     * @param element_index  Index into config.processing.elements (Offset only).
     */
    void configure(const engine::core::config::PipelineConfig& config, Mode mode,
                   int element_index = -1);

    /**
     * @brief Override offset formula with explicit GIE→offset map.
     */
    void set_explicit_offsets(const std::unordered_map<int, int>& offsets);

    /** @brief Static GStreamer pad probe callback. */
    static GstPadProbeReturn on_buffer(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);

private:
    Mode mode_ = Mode::Offset;
    int gie_unique_id_ = 0;     ///< Offset mode: which GIE this handler targets
    int offset_step_ = 1000;    ///< Multiplier for offset formula
    int base_offset_ = 0;       ///< Precomputed: gie_unique_id_ * offset_step_

    std::unordered_map<int, int> explicit_offsets_;

    static constexpr int64_t MAGIC_MARKER = 0x4C4E5441;  // "LNTA"
};
}
```

### ProbeHandlerManager Dispatch (probe_handler_manager.cpp)

```cpp
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

---

## 8. When to Enable

| Scenario                          | `class_id_offset`                                   | `class_id_restore` |
| --------------------------------- | --------------------------------------------------- | ------------------ |
| Single PGIE, no SGIE              | `enable: false`                                     | `enable: false`    |
| PGIE + 1 SGIE (no label conflict) | `enable: false`                                     | `enable: false`    |
| PGIE + multiple SGIEs             | `enable: true`                                      | `enable: true`     |
| Label filter failing unexpectedly | Check if class_id collision exists → `enable: true` |

---

## 9. Liên kết

- [`docs/architecture/deepstream/07_event_handlers_probes.md`](../deepstream/07_event_handlers_probes.md) — Tổng quan probe system
- [`docs/architecture/RAII.md`](../RAII.md) — RAII patterns cho GStreamer resources
- Config example: [`docs/configs/deepstream_default.yml`](../../configs/deepstream_default.yml)
