# CropObject Probe Handler

## 1. Tổng Quan

`CropObjectHandler` là pad probe cắt (crop) detected objects từ GPU frame thành ảnh JPEG, sử dụng **NvDsObjEnc** CUDA-accelerated encoder. Handler publish metadata dạng JSON đến message broker (Redis Streams) để downstream consumers xử lý.

### Vai trò trong pipeline

```
nvmultiurisrcbin → nvinfer(PGIE) → nvtracker ─── src pad ───▶ OSD → sink
                                                       │
                                             CropObjectHandler
                                             (attached to tracker:src)
                                                       │
                                             ┌─────────┴──────────┐
                                             │   Per-object ops   │
                                             ├────────────────────┤
                                             │ 1. decide_capture()│
                                             │ 2. payload hash    │
                                             │ 3. encode JPEG     │
                                             │ 4. accumulate msg  │
                                             └────────────────────┘
                                                       │
                                             nvds_obj_enc_finish()
                                                       │
                                             publish_pending_messages()
                                                       ▼
                                                Redis Streams
```

Handler gắn vào `src` pad của element được chỉ định trong `probe_element` (thường là `tracker`). Mỗi batch buffer đi qua, handler sẽ:

1. Map `NvBufSurface`, `cudaDeviceSynchronize()`
2. Iterate frame → object metadata → áp dụng `label_filter`
3. Với mỗi object đủ điều kiện: quyết định capture, encode crop JPEG, accumulate pending message
4. Gọi `nvds_obj_enc_finish()` — đảm bảo tất cả file JPEG đã ghi xong
5. Publish tất cả pending messages đến broker

---

## 2. Cấu Hình YAML

### 2.1 Crop Objects Handler Entry

```yaml
event_handlers:
  - id: crop_objects
    enable: true
    type: on_detect
    probe_element: tracker # Attach probe tới element có id này
    trigger: crop_objects # pad_name mặc định là "src"
    label_filter:
      - bike
      - bus
      - car
      - person
      - truck
    save_dir: "/opt/vms_engine/dev/rec/objects"
    capture_interval_sec: 5 # Khoảng cách tối thiểu giữa 2 lần crop cùng object (PTS-based)
    image_quality: 85 # JPEG quality 1-100
    save_full_frame: true # Lưu full-frame cùng crop
    channel: worker_lsr_snap # Redis Stream / Kafka topic; để trống = không publish
    cleanup:
      stale_object_timeout_min: 5 # Xóa object state sau N phút không thấy
      check_interval_batches: 30 # Kiểm tra cleanup mỗi N batches
      old_dirs_max_days: 7 # Xóa daily directories cũ hơn N ngày (0=tắt)
    ext_processor:
      enable: true
      min_interval_sec: 1
      rules:
        - label: face
          endpoint: http://192.168.1.99:8000/api/v1/face/recognize/upload
          result_path: match.external_id
          display_path: match.face_name
          params:
            threshold: "0.65"
            skip_anti_spoofing: "false"
```

### 2.2 Field Reference

| Field                                | Type     | Default | Mô tả                                                              |
| ------------------------------------ | -------- | ------- | ------------------------------------------------------------------ |
| `id`                                 | string   | —       | Unique ID trong `event_handlers`                                   |
| `enable`                             | bool     | `true`  | `false` = handler bị bỏ qua                                        |
| `probe_element`                      | string   | —       | ID element để gắn probe (thường `tracker`)                         |
| `trigger`                            | string   | —       | Phải là `"crop_objects"`                                           |
| `pad_name`                           | string   | `"src"` | Pad để gắn probe                                                   |
| `label_filter`                       | string[] | `[]`    | Label khớp; rỗng = tất cả                                          |
| `save_dir`                           | string   | —       | Thư mục gốc lưu crop images                                        |
| `capture_interval_sec`               | int      | `5`     | PTS-based throttle giữa 2 lần crop cùng 1 object                   |
| `image_quality`                      | int      | `85`    | JPEG quality 1–100                                                 |
| `save_full_frame`                    | bool     | `true`  | Lưu full-frame cùng crop                                           |
| `cleanup.stale_object_timeout_min`   | int      | `5`     | Xóa object state nếu không thấy sau N phút                         |
| `cleanup.check_interval_batches`     | int      | `30`    | Chạy cleanup mỗi N batches                                         |
| `cleanup.old_dirs_max_days`          | int      | `7`     | Xóa daily dirs cũ hơn N ngày; 0=tắt                                |
| `channel`                            | string   | `""`    | Redis Stream / Kafka topic để publish events; rỗng = không publish |
| `ext_processor.enable`               | bool     | `false` | Bật external processor (chưa implement runtime, chỉ parse config)  |
| `ext_processor.min_interval_sec`     | int      | `1`     | Khoảng cách tối thiểu giữa 2 lần gọi ext processor                 |
| `ext_processor.rules[].label`        | string   | —       | Label trigger external processing                                  |
| `ext_processor.rules[].endpoint`     | string   | —       | HTTP endpoint cho external processor                               |
| `ext_processor.rules[].result_path`  | string   | —       | JSON path lấy result                                               |
| `ext_processor.rules[].display_path` | string   | —       | JSON path lấy display text                                         |
| `ext_processor.rules[].params`       | map      | `{}`    | Key-value params gửi kèm request                                   |

---

## 3. Kiến Trúc Handler

### 3.1 Publish Decision System

Handler sử dụng `PubDecisionType` để phân loại lý do publish:

```cpp
enum class PubDecisionType {
    None,       // Không publish (throttled hoặc dedup suppressed)
    FirstSeen,  // Object lần đầu xuất hiện (tracker ID mới)
    Heartbeat   // Periodic re-publish sau capture_interval_sec
};
```

**Decision flow cho mỗi object:**

```
object detected
    │
    ▼
last_capture_pts_ có key?
    │
    ├── KHÔNG → PubDecisionType::FirstSeen (capture ngay)
    │
    └── CÓ → elapsed = current_pts - last_capture_pts
              │
              ├── elapsed < capture_interval_ns → PubDecisionType::None (skip)
              │
              └── elapsed >= capture_interval_ns → PubDecisionType::Heartbeat
                    │
                    ▼
              compute_payload_hash(class_id, label, bbox)
                    │
                    ├── hash == last_payload_hash → suppressed (dedup)
                    │
                    └── hash != last_payload_hash → publish Heartbeat
```

### 3.2 Per-Object State (`ObjectPubState`)

```cpp
struct ObjectPubState {
    GstClockTime last_publish_pts = GST_CLOCK_TIME_NONE;
    uint64_t heartbeat_seq = 0;           // Đếm heartbeat sequence
    std::size_t last_payload_hash = 0;    // Hash cho heartbeat dedup
    std::string last_message_id;          // Message ID cuối cùng (chain correlation)
};
```

Mỗi object (keyed bằng `compose_key(source_id, tracker_id)`) có state riêng:

- **`heartbeat_seq`**: Reset về 0 khi FirstSeen, tăng dần mỗi Heartbeat
- **`last_payload_hash`**: So sánh để suppress heartbeat trùng lặp
- **`last_message_id`**: Tạo chain `mid` → `prev_mid` cho correlation

### 3.3 Payload Hash Dedup

Heartbeat suppression dựa trên hash kết hợp: `class_id + label + quantized_bbox`

```cpp
// Quantize bbox to integer pixels để tránh floating-point jitter
int q_left = static_cast<int>(left);
int q_top = static_cast<int>(top);
// ... Boost hash combine pattern
```

Khi object đứng yên (bbox không thay đổi đáng kể + cùng label/class), heartbeat bị suppress → giảm lượng message publish.

### 3.4 Batch-Accumulate-Then-Publish Pattern

```
 ┌─────────── Process Batch ──────────┐
 │                                     │
 │  for each frame:                    │
 │    for each object:                 │
 │      decide_capture() → decision    │
 │      payload hash → dedup check     │
 │      nvds_obj_enc_process() → queue │ ← CUDA encode queued
 │      pending_messages.push_back()   │ ← message accumulated
 │                                     │
 │  nvds_obj_enc_finish()              │ ← ALL JPEGs written to disk
 │                                     │
 │  publish_pending_messages()         │ ← ALL messages published
 └─────────────────────────────────────┘
```

**Tại sao batch-accumulate:**

1. `nvds_obj_enc_finish()` phải được gọi SAU khi tất cả encode đã queued — nó block cho đến khi tất cả JPEG ghi xong
2. Publish messages PHẢI xảy ra SAU `nvds_obj_enc_finish()` — đảm bảo consumer nhận message khi file đã tồn tại trên disk
3. Giảm thời gian giữ mutex — accumulate nhanh, publish một lần

### 3.5 State Maps

Handler duy trì 4 state maps song song, keyed bằng `compose_key(source_id, tracker_id)`:

| Map                 | Type                                      | Mô tả                                |
| ------------------- | ----------------------------------------- | ------------------------------------ |
| `object_keys_`      | `unordered_map<uint64_t, string>`         | Persistent UUIDv7 cho mỗi tracker ID |
| `object_last_seen_` | `unordered_map<uint64_t, GstClockTime>`   | PTS lần cuối thấy object             |
| `last_capture_pts_` | `unordered_map<uint64_t, GstClockTime>`   | PTS lần cuối capture                 |
| `pub_state_`        | `unordered_map<uint64_t, ObjectPubState>` | Publish state (hash, seq, mid)       |

**Cleanup đồng bộ**: Khi xóa stale object, TẤT CẢ 4 maps đều được dọn dẹp (không để orphan entries).

---

## 4. CUDA Encoding — NvDsObjEnc

### 4.1 Quy trình encode

```cpp
// 1. Tạo encoder context (một lần trong configure)
enc_ctx_ = nvds_obj_enc_create_context(0);  // GPU 0

// 2. Queue encode task cho mỗi object
NvDsObjEncUsrArgs enc_args = {};
enc_args.saveImg = TRUE;
enc_args.quality = image_quality_;
enc_args.isFrame = 0;  // 0=crop, 1=full-frame
snprintf(enc_args.fileNameImg, ..., crop_path.c_str());
nvds_obj_enc_process(enc_ctx_, &enc_args, ip_surf, obj, frame_meta);

// 3. Finish (PHẢI gọi mỗi batch)
nvds_obj_enc_finish(enc_ctx_);  // Block đến khi tất cả JPEG ghi xong
```

### 4.2 cudaDeviceSynchronize()

```cpp
// CRITICAL: Sync trước khi encode
cudaDeviceSynchronize();
```

Nếu không sync, decoder có thể chưa ghi xong GPU buffer → encoder đọc data bị partial/stale → ảnh JPEG bị mờ hoặc corrupted.

### 4.3 Full-Frame Dedup

Full-frame chỉ capture **một lần per frame number per batch**:

```cpp
// Keyed by frame_num — nếu đã capture cho frame này, reuse path
auto ff_it = full_frame_paths_this_batch_.find(frame_num_key);
if (ff_it != full_frame_paths_this_batch_.end()) {
    ff_path = ff_it->second;  // Reuse
} else {
    // Encode + lưu path vào map
}
```

---

## 5. Message Publishing — JSON Schema

### 5.1 Published JSON Structure

```json
{
  "event": "object_detected",
  "pipeline_id": "pipeline_01",
  "source_id": 0,
  "source_name": "camera-01",
  "object_key": "019785f2-...",
  "instance_key": "019785f3-...",
  "class_id": 2,
  "label": "car",
  "confidence": 0.92,
  "tracker_id": 42,
  "bbox": {
    "left": 120.5,
    "top": 200.3,
    "width": 80.0,
    "height": 60.0
  },
  "crop_path": "/opt/vms_engine/dev/rec/objects/20250711/src_0/crop_car_42_019785f3.jpg",
  "full_frame_path": "/opt/vms_engine/dev/rec/objects/20250711/src_0/ff_0_f1234.jpg",
  "timestamp_ms": 1720700000000,
  "frame_num": 1234,
  "mid": "src_0_f1234_1720700000000",
  "pub_type": "first_seen",
  "heartbeat_seq": 0,
  "prev_mid": ""
}
```

### 5.2 Field Description

| Field             | Type   | Mô tả                                                           |
| ----------------- | ------ | --------------------------------------------------------------- |
| `event`           | string | Luôn là `"object_detected"`                                     |
| `pipeline_id`     | string | Pipeline ID từ config                                           |
| `source_id`       | int    | Camera source index                                             |
| `source_name`     | string | Camera ID từ `sources.cameras[source_id].id`                    |
| `object_key`      | string | Persistent UUIDv7 cho tracker object (stable across heartbeats) |
| `instance_key`    | string | UUIDv7 unique cho mỗi capture instance                          |
| `class_id`        | int    | Detection class ID                                              |
| `label`           | string | Detection label                                                 |
| `confidence`      | float  | Detection confidence                                            |
| `tracker_id`      | uint64 | Tracker-assigned object ID                                      |
| `bbox`            | object | Bounding box coordinates                                        |
| `crop_path`       | string | Đường dẫn file crop JPEG                                        |
| `full_frame_path` | string | Đường dẫn file full-frame (rỗng nếu tắt)                        |
| `timestamp_ms`    | int64  | Wall-clock epoch milliseconds                                   |
| `frame_num`       | uint64 | GStreamer frame number                                          |
| `mid`             | string | Message ID — unique cho mỗi message                             |
| `pub_type`        | string | `"first_seen"` hoặc `"heartbeat"`                               |
| `heartbeat_seq`   | uint64 | Heartbeat sequence (0 cho first_seen, tăng dần)                 |
| `prev_mid`        | string | Message ID trước đó của cùng object (chain correlation)         |

### 5.3 Message ID Chain

```
Object xuất hiện lần đầu:
  mid="src_0_f100_17207...", pub_type="first_seen", heartbeat_seq=0, prev_mid=""

5 giây sau (heartbeat):
  mid="src_0_f250_17207...", pub_type="heartbeat", heartbeat_seq=1, prev_mid="src_0_f100_17207..."

10 giây sau (heartbeat):
  mid="src_0_f400_17207...", pub_type="heartbeat", heartbeat_seq=2, prev_mid="src_0_f250_17207..."
```

Consumer có thể dùng `object_key` + `prev_mid` để xây dựng event timeline cho mỗi object.

---

## 6. Directory Structure

### 6.1 Daily Rotation

```
save_dir/
  └── 20250711/          ← YYYYMMDD format, tự tạo mỗi ngày
       ├── src_0/        ← per-source subdirectory
       │    ├── crop_car_42_019785f3.jpg
       │    ├── crop_person_7_019785f4.jpg
       │    └── ff_0_f1234.jpg
       └── src_1/
            └── ...
```

### 6.2 File Naming

| File Type  | Format                                            | Ví dụ                      |
| ---------- | ------------------------------------------------- | -------------------------- |
| Crop       | `crop_{sanitized_label}_{tracker_id}_{uuid8}.jpg` | `crop_car_42_019785f3.jpg` |
| Full-frame | `ff_{source_id}_f{frame_num}.jpg`                 | `ff_0_f1234.jpg`           |

**Label sanitization**: Non-alphanumeric chars (trừ `-` và `_`) → `_`, truncate 30 chars.

### 6.3 Old Directory Cleanup

- Điều kiện: `old_dirs_max_days > 0`
- Rate limit: tối đa 1 lần/giờ
- Match directory name pattern: 8 digits (`YYYYMMDD`)
- So sánh integer `YYYYMMDD < threshold_YYYYMMDD` → xóa

---

## 7. Cleanup & Memory Management

### 7.1 Stale Object Cleanup

Mỗi `check_interval_batches` batch:

1. Iterate `object_last_seen_` — tìm entries có `(current_pts - last_seen) > timeout_ns`
2. Xóa đồng bộ từ TẤT CẢ 4 maps: `object_keys_`, `object_last_seen_`, `last_capture_pts_`, `pub_state_`
3. Log số lượng removed

### 7.2 Emergency Hard Limit

```cpp
static constexpr size_t MAX_TRACKED_OBJECTS = 5000;

size_t max_map = std::max({object_keys_.size(), object_last_seen_.size(),
                           last_capture_pts_.size(), pub_state_.size()});
if (max_map > MAX_TRACKED_OBJECTS) {
    // EMERGENCY: clear ALL maps
}
```

Prevent unbounded memory growth từ tracker ID churn (ví dụ: crowded scene với nhiều new/lost objects).

### 7.3 Memory Stats Logging

Mỗi cleanup cycle, log approximate memory usage:

```
CropObjectHandler: memory stats — object_keys=42, last_seen=42, capture_pts=38, pub_state=42, ~7KB
```

Approximate size per entry:

- `object_keys_`: ~64 bytes (key + string)
- `object_last_seen_`: ~16 bytes
- `last_capture_pts_`: ~16 bytes
- `pub_state_`: ~88 bytes (ObjectPubState)

---

## 8. Teardown Safety

```cpp
// Destructor
CropObjectHandler::~CropObjectHandler() {
    shutting_down_.store(true, std::memory_order_release);
    if (enc_ctx_) {
        nvds_obj_enc_destroy_context(enc_ctx_);
    }
}

// on_buffer checks early
if (self->shutting_down_.load(std::memory_order_acquire)) {
    return GST_PAD_PROBE_OK;
}
```

`shutting_down_` atomic flag ngăn probe callback truy cập resources sau khi destructor bắt đầu.

---

## 9. Cải Tiến So Với V1

| Feature                  | V1 (cũ)                 | V2 (hiện tại)                                       |
| ------------------------ | ----------------------- | --------------------------------------------------- |
| **Publish decision**     | Capture = publish       | `PubDecisionType` (FirstSeen/Heartbeat/None)        |
| **Heartbeat dedup**      | Không có                | Payload hash so sánh → suppress duplicate heartbeat |
| **Publish timing**       | Publish ngay khi encode | Batch-accumulate → finish → publish                 |
| **Message ID chain**     | Không có                | `mid` + `prev_mid` cho event correlation            |
| **Stale cleanup scope**  | 3 maps                  | 4 maps (thêm `pub_state_`)                          |
| **Memory stats**         | Không có                | `log_memory_stats()` mỗi cleanup cycle              |
| **Emergency limit**      | 10000                   | 5000 (stricter)                                     |
| **File naming**          | `crop_{tid}_{uuid8}`    | `crop_{label}_{tid}_{uuid8}` (sanitized label)      |
| **ext_processor config** | Không parse             | Parsed vào `ExtProcessorConfig` struct              |
| **JSON new fields**      | —                       | `mid`, `pub_type`, `heartbeat_seq`, `prev_mid`      |

### Từ lantanav2 — Những gì KHÔNG port

- **Parent-child grouping / hysteresis**: Yêu cầu SGIE metadata → quá phức tạp cho pipeline hiện tại
- **Token bucket bypass**: Coupled với parent-child → bỏ
- **GstClockRef RAII wrapper**: vms-engine dùng PTS, không cần GstSystemClock reference
- **WSL detection**: Không cần trong container environment
- **ExternalProcessingService implementation**: Config parsed nhưng runtime ext_processor chưa implement (future extension)

---

## 10. ext_processor — Future Extension

Config struct đã được parse vào `ExtProcessorConfig`:

```cpp
struct ExtProcessorRule {
    std::string label;       // Label trigger
    std::string endpoint;    // HTTP endpoint
    std::string result_path; // JSON path lấy result
    std::string display_path;// JSON path lấy display text
    std::unordered_map<std::string, std::string> params;
};

struct ExtProcessorConfig {
    bool enable = false;
    int min_interval_sec = 1;
    std::vector<ExtProcessorRule> rules;
};
```

**Planned behavior** (khi implement):

1. Sau khi crop JPEG ghi xong, nếu object label match rule → gửi HTTP POST đến endpoint
2. Parse response theo `result_path` / `display_path` → thêm vào JSON message
3. Rate-limit theo `min_interval_sec` per label per object
4. Fail gracefully — timeout/error không ảnh hưởng pipeline

---

## 11. Troubleshooting

### Ảnh JPEG bị mờ / corrupted

```
Nguyên nhân: cudaDeviceSynchronize() thất bại hoặc bị skip
Kiểm tra: LOG warning "cudaDeviceSynchronize failed"
Fix: Đảm bảo GPU không bị overload, kiểm tra CUDA driver version
```

### Không có file JPEG nào được tạo

```
Nguyên nhân: Encoder context null
Kiểm tra: LOG error "encoder context null"
Fix: Kiểm tra GPU ID, CUDA availability, DeepStream encoder libs
```

### Memory tăng không ngừng

```
Nguyên nhân: Tracker ID churn (crowded scene, many new/lost objects)
Kiểm tra: LOG warning "EMERGENCY — map size exceeded"
Fix:
  - Giảm stale_object_timeout_min (vd: 2 thay vì 5)
  - Giảm check_interval_batches (vd: 10 thay vì 30)
  - Kiểm tra tracker config — tracker đang assign quá nhiều new IDs
```

### Message publish thất bại

```
Nguyên nhân: Redis connection issue
Kiểm tra: LOG warning "publish failed"
Fix: Kiểm tra broker host/port, Redis availability
```

### Heartbeat bị suppress quá nhiều

```
Nguyên nhân: Object đứng yên, bbox không thay đổi → payload hash giống nhau
Expected behavior: Đây là dedup feature — khi object đứng yên, chỉ publish khi bbox thay đổi
Fix: Nếu cần publish mọi heartbeat, đặt capture_interval_sec = 0 hoặc tắt dedup logic
```
