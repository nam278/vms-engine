# CropObject Probe Handler

## 1. Tổng Quan

`CropObjectHandler` là pad probe cắt (crop) detected objects từ GPU frame thành ảnh JPEG, sử dụng **NvDsObjEnc** CUDA-accelerated encoder. Handler publish metadata dạng JSON đến message broker (Redis Streams) để downstream consumers xử lý. Phần stale cleanup hiện vẫn tạo `Exit` metadata nội bộ nhưng chưa publish ra broker.

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
2. Iterate frame → build parent/child groups từ metadata (`ObjectGroup`)
3. Với mỗi group: xử lý parent trước, children sau theo **strict co-publish default** (parent phải publish thì child mới được publish cùng lượt)
4. Gọi `nvds_obj_enc_finish()` — đảm bảo tất cả file JPEG đã ghi xong
5. Publish tất cả pending messages đến broker
6. Chạy maintenance định kỳ (stale cleanup, old dir cleanup, memory stats)

> Ghi chú maintainability: implementation đã được tách theo phase `process_batch -> process_frame -> build_object_groups/process_object_for_publish` để giảm độ phình của hàm chính và dễ review từng bước.

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
    Bypass,     // Burst capture within interval (token bucket)
    Heartbeat,  // Periodic re-publish sau capture_interval_sec
    Exit        // Object bị xóa do stale timeout (metadata-only, no image)
};
```

**Decision flow theo parent-group (strict default):**

```
parent-group detected
    │
    ▼
last_capture_pts_ của parent có key?
    │
    ├── KHÔNG → PubDecisionType::FirstSeen
    │           (capture ngay, init bypass_tokens=BURST_MAX,
    │            last_sgie_labels=group_sgie_signature  ← baseline cho bypass sau)
    │
    └── CÓ → elapsed = current_pts - last_capture_pts
              │
              ├── elapsed >= capture_interval_ns
              │     │
              │     └── refill_tokens() → PubDecisionType::Heartbeat
              │           │
              │           ▼
              │     compute_payload_hash(class_id, parent_label, group_sgie_signature, bbox)
              │           ├── hash == last_payload_hash → suppressed (dedup)
              │           └── hash != last_payload_hash → publish Heartbeat
              │
              └── elapsed < capture_interval_ns
                    │
                    └── refill_tokens()
                            └── group_sgie_signature không rỗng
                              AND group_sgie_signature != state.last_sgie_labels  ← LABEL CHANGED
                                AND attempt_bypass() → tokens > 0?
                                      ├── CÓ  → PubDecisionType::Bypass (label-change burst)
                                      └── KHÔNG → PubDecisionType::None (skip)
```

                  Trong đó `group_sgie_signature = parent_sgie_labels || aggregated_child_sgie_labels` chỉ dùng để quyết định `first_seen/bypass/heartbeat` cho parent-group.
                  Khi publish message của parent, field `labels` vẫn chỉ chứa classifier labels của chính parent (nếu parent classify rỗng thì `labels=""`), không merge labels của child.

                  `ObjectPubState.last_sgie_labels` lưu baseline theo **decision signature** (`group_sgie_signature`) chứ không phải theo payload `labels` của parent. Vì vậy, khi signature không đổi thì bypass không bị bắn lặp mỗi frame.

**Token Bucket (Bypass) — Chỉ bắn khi SGIE label THAY ĐỔI**:

- `K_ON_FRAMES = 5`: object/group phải đạt 5 lần quan sát ổn định trước khi publish (kết hợp với `K_OFF_FRAMES`)
- `K_OFF_FRAMES = 2`: cho phép khoảng cách tối đa 2 frame giữa 2 lần quan sát khi tính ON hysteresis (chịu jitter scheduler)
- `K_LABEL_FRAMES = 5`: signature label thay đổi phải giữ ổn định 5 frame liên tiếp mới cho bypass
- `BURST_MAX = 3`: Maximum tokens per object
- `TOKEN_REFILL_NS = 5s`: Refill 1 token after every 5 seconds (PTS-based)
- `BYPASS_MIN_GAP_NS = 1s`: Debounce tối thiểu giữa 2 bypass liên tiếp của cùng object
- Các ngưỡng trên hiện có thể cấu hình trực tiếp trong YAML `event_handlers[].crop_objects`:
  `burst_max`, `k_on_frames`, `k_off_frames`, `k_label_frames`, `token_refill_sec`, `bypass_min_gap_sec`
- FirstSeen: init `bypass_tokens = BURST_MAX`, set `last_sgie_labels = sgie_labels` làm baseline
- Bypass fires **chỉ khi** `sgie_labels != state.last_sgie_labels` (label change) **VÀ** token > 0
- Nếu khoảng cách từ lần publish trước `< BYPASS_MIN_GAP_NS` thì bypass bị chặn (trả `None`)
- Mỗi bypass consume 1 token; không increment `heartbeat_seq`
- Nếu không có SGIE (sgie_labels rỗng): bypass không bao giờ fire
- `last_sgie_labels` được cập nhật ở commit block → baseline cho frame tiếp theo

Aligned với lantanav2 `LabelStateV2` pattern: burst capture khi object state thay đổi (không phải burst vô điều kiện).

**Exit Messages**:

- Generated during stale object cleanup
- Metadata-only: no crop/full-frame images
- `fname` and `fname_ff` are empty strings
- `pub_type = "exit"`, `pub_reason = "Object removed (stale cleanup)"`
- Hiện tại các `Exit` messages này chỉ được tạo nội bộ từ `cleanup_stale_objects()` để phục vụ state lifecycle; chưa được gọi `publish_pending_messages()` ra broker.

### 3.2 Per-Object State (`ObjectPubState`)

```cpp
struct ObjectPubState {
    GstClockTime last_publish_pts = GST_CLOCK_TIME_NONE;
    uint64_t heartbeat_seq = 0;           // Đếm heartbeat sequence
    std::size_t last_payload_hash = 0;    // Hash cho heartbeat dedup
    std::string last_message_id;          // Message ID cuối cùng (chain correlation)
  std::string last_instance_key;        // instance_key lần publish gần nhất
    int bypass_tokens = 0;                // Token bucket cho Bypass burst capture
    GstClockTime last_refill_pts = GST_CLOCK_TIME_NONE; // PTS lần cuối refill tokens
    std::string last_sgie_labels;         // SGIE label string lần cuối publish (bypass on change)
};
```

Mỗi object (keyed bằng `compose_key(source_id, tracker_id)`) có state riêng:

- **`heartbeat_seq`**: Reset về 0 khi FirstSeen, tăng dần mỗi Heartbeat (Bypass không tăng)
- **`last_payload_hash`**: So sánh để suppress heartbeat trùng lặp
- **`last_message_id`**: Tạo chain `mid` → `prev_mid` cho correlation
- **`last_instance_key`**: Lưu `instance_key` gần nhất để child có thể reuse làm `parent_instance_key` khi parent bị heartbeat-dedup
- **`bypass_tokens`**: Init = `BURST_MAX` (3) khi FirstSeen, giảm dần mỗi Bypass, refill mỗi `TOKEN_REFILL_NS` (5s)
- **`last_refill_pts`**: PTS lần cuối refill — dùng để tính số tokens được thêm
- **`last_sgie_labels`**: SGIE label string lần cuối được commit, set = `sgie_labels` tại FirstSeen làm baseline; bypass fires khi string này thay đổi

### 3.3 Payload Hash Dedup

Heartbeat suppression dựa trên hash kết hợp: `class_id + label + sgie_labels + quantized_bbox`

```cpp
// Quantize bbox to integer pixels để tránh floating-point jitter
int q_left = static_cast<int>(left);
int q_top  = static_cast<int>(top);
// Boost hash combine pattern — tất cả 7 thành phần:
seed ^= hash(class_id)
seed ^= hash(label)       // PGIE class name
seed ^= hash(sgie_labels) // SGIE classifier output — nếu label đổi, hash đổi → dedup flush
seed ^= hash(q_left) ^ hash(q_top) ^ hash(q_width) ^ hash(q_height)
```

Việc include `sgie_labels` vào hash đảm bảo: khi SGIE re-classify object (label thay đổi), hash cũ bị invalidate → heartbeat dedup không suppress publish đó.

Khi object đứng yên (bbox không đổi + cùng label + cùng SGIE labels), heartbeat bị suppress → giảm lượng message publish.

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

Handler duy trì 5 state maps chính. Trong đó 4 map keyed bằng `compose_key(source_id, tracker_id)`, và 1 map parent-cache keyed bằng child `compose_key`:

| Map                       | Type                                      | Mô tả                                                                                           |
| ------------------------- | ----------------------------------------- | ----------------------------------------------------------------------------------------------- |
| `object_keys_`            | `unordered_map<uint64_t, string>`         | Persistent UUIDv7 cho mỗi tracker ID                                                            |
| `object_last_seen_`       | `unordered_map<uint64_t, GstClockTime>`   | PTS lần cuối thấy object                                                                        |
| `last_capture_pts_`       | `unordered_map<uint64_t, GstClockTime>`   | PTS lần cuối capture                                                                            |
| `pub_state_`              | `unordered_map<uint64_t, ObjectPubState>` | Publish state (hash, seq, mid)                                                                  |
| `child_parent_oid_cache_` | `unordered_map<uint64_t, uint64_t>`       | Cache quan hệ `compose_key(src, child_tid) -> parent_tid` khi `obj->parent` bị null sau tracker |

**Cleanup đồng bộ**: Khi xóa stale object, state object được dọn từ `object_keys_`, `object_last_seen_`, `last_capture_pts_`, `pub_state_`, đồng thời xóa entry tương ứng trong `child_parent_oid_cache_`.

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

Field structure aligned with lantanav2 `CropObjectHandlerV2` Redis XADD format. vms-engine publishes via `IMessageProducer::publish_json()` — **Redis: flat fields** (no `data` wrapper), Kafka: JSON string.

```json
{
  "event": "crop_bb",
  "pid": "pipeline_01",
  "sid": 0,
  "sname": "camera-01",
  "instance_key": "019785f3-...",
  "oid": 42,
  "object_key": "019785f2-...",
  "parent_object_key": "019785f2-parent...",
  "parent": "41",
  "parent_instance_key": "019785f3-parent-inst...",
  "class": "car",
  "conf": 0.92,
  "labels": "sport_car:0:0.87|sedan:1:0.11",
  "top": 200.3,
  "left": 120.5,
  "w": 80.0,
  "h": 60.0,
  "s_w_ff": 1920,
  "s_h_ff": 1080,
  "w_ff": 1920,
  "h_ff": 1080,
  "fname": "20250711/s0_RT20250711_143022_456_car_id42.jpg",
  "fname_ff": "20250711/s0_RT20250711_143022_456_frame_ff.jpg",
  "event_ts": "1720700000000",
  "mid": "019785f3-...",
  "prev_mid": "",
  "pub_type": "first_seen",
  "pub_reason": "New object detected",
  "hb_seq": 0,
  "class_id": 2,
  "frame_num": 1234,
  "tracker_id": 42
}
```

### 5.2 Field Description

| Field                 | Type   | Mô tả                                                                                                                  |
| --------------------- | ------ | ---------------------------------------------------------------------------------------------------------------------- |
| `event`               | string | Luôn là `"crop_bb"` (aligned với lantanav2)                                                                            |
| `pid`                 | string | Pipeline ID từ config                                                                                                  |
| `sid`                 | int    | Camera source index                                                                                                    |
| `sname`               | string | Camera ID từ `sources.cameras[source_id].id`                                                                           |
| `instance_key`        | string | UUIDv7 unique cho mỗi capture instance                                                                                 |
| `oid`                 | uint64 | Tracker-assigned object ID                                                                                             |
| `object_key`          | string | Persistent UUIDv7 cho tracker object (stable across heartbeats)                                                        |
| `parent_object_key`   | string | Parent object key (có giá trị cho SGIE child; rỗng cho top-level parent object)                                        |
| `parent`              | string | Parent tracker id dạng string (có giá trị cho SGIE child; rỗng cho top-level object)                                   |
| `parent_instance_key` | string | Parent instance key (reuse từ parent hiện tại hoặc gần nhất khi parent dedup)                                          |
| `class`               | string | PGIE detection label (`obj_label`, e.g. `"car"`)                                                                       |
| `conf`                | float  | PGIE detection confidence                                                                                              |
| `labels`              | string | SGIE classifier output của chính object hiện tại (parent không gộp labels của child; child giữ labels riêng của child) |
| `top`                 | float  | Bounding box top coordinate                                                                                            |
| `left`                | float  | Bounding box left coordinate                                                                                           |
| `w`                   | float  | Bounding box width                                                                                                     |
| `h`                   | float  | Bounding box height                                                                                                    |
| `s_w_ff`              | int    | Source frame width (từ NvBufSurface)                                                                                   |
| `s_h_ff`              | int    | Source frame height (từ NvBufSurface)                                                                                  |
| `w_ff`                | int    | Pipeline output width (từ config)                                                                                      |
| `h_ff`                | int    | Pipeline output height (từ config)                                                                                     |
| `fname`               | string | Relative path to crop JPEG (từ save_dir)                                                                               |
| `fname_ff`            | string | Relative path to full-frame JPEG (rỗng nếu tắt hoặc Exit)                                                              |
| `event_ts`            | string | Wall-clock epoch milliseconds dạng string                                                                              |
| `mid`                 | string | Message ID — UUIDv7 unique per message                                                                                 |
| `prev_mid`            | string | Message ID trước đó của cùng object (chain correlation)                                                                |
| `pub_type`            | string | `"first_seen"`, `"bypass"`, `"heartbeat"`, `"exit"`                                                                    |
| `pub_reason`          | string | Human-readable reason cho publish decision                                                                             |
| `hb_seq`              | int    | Heartbeat sequence (0 cho first_seen, tăng dần cho heartbeat)                                                          |
| `class_id`            | int    | DeepStream class ID (extra — không có trong lantanav2)                                                                 |
| `frame_num`           | uint64 | GStreamer frame number (extra)                                                                                         |
| `tracker_id`          | uint64 | Tracker ID (extra — giống `oid` nhưng tên khác cho rõ)                                                                 |

### 5.3 pub_type Values

| pub_type     | Khi nào                                                                   | Image | hb_seq            |
| ------------ | ------------------------------------------------------------------------- | ----- | ----------------- |
| `first_seen` | Object xuất hiện lần đầu (tracker ID mới)                                 | Có    | 0                 |
| `bypass`     | SGIE label thay đổi trong interval + token available (label-change burst) | Có    | Giữ nguyên seq cũ |
| `heartbeat`  | Periodic re-publish sau capture_interval_sec                              | Có    | Tăng +1           |
| `exit`       | Object bị xóa do stale timeout                                            | Không | Giữ nguyên seq cũ |

### 5.4 Message ID Chain

```
Object xuất hiện lần đầu (SGIE chưa có kết quả):
  mid="019785f3-...", pub_type="first_seen", hb_seq=0, prev_mid=""
  class="car", labels=""

SGIE classify xong → labels thay đổi "" → "sport_car:0:0.87|sedan:1:0.11" (bypass fires):
  mid="019785f4-...", pub_type="bypass", hb_seq=0, prev_mid="019785f3-..."
  class="car", labels="sport_car:0:0.87|sedan:1:0.11"

SGIE classify lại → labels thay đổi → bypass lần 2 (nếu còn token):
  mid="019785f5-...", pub_type="bypass", hb_seq=0, prev_mid="019785f4-..."
  class="car", labels="sedan:1:0.95"

5 giây sau (heartbeat):
  mid="019785f6-...", pub_type="heartbeat", hb_seq=1, prev_mid="019785f5-..."
  class="car", labels="sedan:1:0.95"

Object biến mất (stale cleanup):
  mid="019785f7-...", pub_type="exit", hb_seq=1, prev_mid="019785f6-..."
  fname="", fname_ff="", labels=""
```

Consumer có thể dùng `object_key` + `prev_mid` để xây dựng event timeline cho mỗi object. `labels` cho phép track quá trình SGIE re-classify qua thời gian.

---

## 6. Directory Structure

### 6.1 Daily Rotation

```
save_dir/
  └── 20250711/          ← YYYYMMDD format, tự tạo mỗi ngày
       ├── s0_RT20250711_143022_456_car_id42.jpg        ← crop
       ├── s0_RT20250711_143022_456_person_id7.jpg      ← crop
       ├── s0_RT20250711_143022_456_frame_ff.jpg        ← full-frame
       ├── s1_RT20250711_143025_789_truck_id15.jpg      ← crop from source 1
       └── ...
```

> **Flat directory** — không có `src_N/` subdirectory (aligned với lantanav2). Tất cả file trong cùng daily dir, phân biệt bằng `s{sid}` prefix.

### 6.2 File Naming (aligned với lantanav2)

| File Type  | Format                                               | Ví dụ                                   |
| ---------- | ---------------------------------------------------- | --------------------------------------- |
| Crop       | `s{sid}_RT{YYYYMMDD_HHMMSS_mmm}_{label}_id{oid}.jpg` | `s0_RT20250711_143022_456_car_id42.jpg` |
| Full-frame | `s{sid}_RT{YYYYMMDD_HHMMSS_mmm}_frame_ff.jpg`        | `s0_RT20250711_143022_456_frame_ff.jpg` |

**Realtime string format**: `YYYYMMDD_HHMMSS_mmm` — generated từ `std::chrono::system_clock::now()`.

**Label sanitization**: Non-alphanumeric chars (trừ `-` và `_`) → `_`, truncate 30 chars.

**fname / fname_ff** trong JSON message là **relative paths** từ `save_dir`:

- `fname`: `"20250711/s0_RT20250711_143022_456_car_id42.jpg"`
- `fname_ff`: `"20250711/s0_RT20250711_143022_456_frame_ff.jpg"`

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
2. Tạo `PendingMessage` với `pub_type = "exit"` cho mỗi stale object (metadata-only, no image paths)
3. Xóa đồng bộ state object: `object_keys_`, `object_last_seen_`, `last_capture_pts_`, `pub_state_`, và entry `child_parent_oid_cache_` cùng key compose
4. Không publish Exit messages ra broker (current behavior)
5. Log số lượng removed

> Nếu cần downstream close timeline theo `exit`, có thể bật lại publish cho vector trả về từ `cleanup_stale_objects()` trong `process_batch()`.

### 7.2 Emergency Hard Limit

```cpp
static constexpr size_t MAX_TRACKED_OBJECTS = 5000;

size_t max_map = std::max({object_keys_.size(), object_last_seen_.size(),
                           last_capture_pts_.size(), pub_state_.size()});
if (max_map > MAX_TRACKED_OBJECTS) {
  // EMERGENCY: clear ALL maps (bao gồm child_parent_oid_cache_)
}
```

Prevent unbounded memory growth từ tracker ID churn (ví dụ: crowded scene với nhiều new/lost objects).

### 7.3 Memory Stats Logging

Mỗi cleanup cycle, log approximate memory usage:

```
CropObjectHandler: memory stats — tracked=42 last_seen=42 capture_pts=38 pub_state=42 parent_cache=17 ~9KB
```

Approximate size per entry:

- `object_keys_`: ~64 bytes (key + string)
- `object_last_seen_`: ~16 bytes
- `last_capture_pts_`: ~16 bytes
- `pub_state_`: ~104 bytes (ObjectPubState hiện tại)
- `child_parent_oid_cache_`: ~16 bytes

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

## 9. So Sánh vms-engine vs lantanav2 CropObjectHandlerV2

### 9.1 Tóm Tắt

| Khía cạnh                    | lantanav2 `CropObjectHandlerV2`                                  | vms-engine `CropObjectHandler`                                  |
| ---------------------------- | ---------------------------------------------------------------- | --------------------------------------------------------------- |
| **Interface**                | `IProbeHandler` plugin, `REGISTER_HANDLER()` macro               | Native GStreamer static probe callback                          |
| **Config**                   | Semicolon-delimited string (parse lúc runtime)                   | Typed C++ structs từ YAML (`PipelineConfig`)                    |
| **Transport**                | `RedisStreamProducer` direct (Redis XADD)                        | `IMessageProducer` abstraction (Redis / Kafka)                  |
| **Message format**           | Redis XADD string pairs — bbox/dims dưới dạng `std::to_string()` | JSON native types — `float` bbox, `int` dims                    |
| **Decision trigger**         | Hysteresis trên child presence (SGIE-driven)                     | Tracker ID mới + PTS interval (PGIE + optional SGIE)            |
| **Parent-child**             | Đầy đủ — group parent+children, quyết định theo child presence   | ✅ Có — group-based 2-pass + `child_parent_oid_cache_` fallback |
| **Label state**              | `LabelStateV2` — Bypass khi SGIE label thay đổi                  | ✅ Có — `last_sgie_labels` tracking, bypass on label change     |
| **WSL support**              | `is_running_on_wsl()` — skip GPU encode khi WSL                  | Không cần — container-native                                    |
| **Payload hash dedup**       | Tracked nhưng không suppress heartbeat trong V2                  | Có — suppress duplicate heartbeat khi object đứng yên           |
| **hb_seq publish**           | Chỉ khi Heartbeat (conditional field push)                       | Luôn có trong mọi pub_type                                      |
| **Memory stats**             | Không log                                                        | `log_memory_stats()` mỗi cleanup cycle                          |
| **Emergency hard limit**     | Không có explicit limit                                          | 5000 objects — full map clear khi vượt                          |
| **Source dims**              | `frame_meta->source_frame_width/Height`                          | Đọc từ `NvBufSurface` (actual GPU buffer)                       |
| **Redis reconnect tracking** | `is_connected()` check trước/sau mỗi send                        | Log warning khi send fail, không re-check mỗi msg               |

---

### 9.2 Giống Nhau — Những gì được giữ nguyên

Vms-engine giữ nguyên tất cả design decisions quan trọng của lantanav2 V2:

| Feature                       | Cả hai đều có                                                                                                                                                                                                                             |
| ----------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Event name                    | `"crop_bb"`                                                                                                                                                                                                                               |
| File naming                   | `s{sid}_RT{YYYYMMDD_HHMMSS_mmm}_{label}_id{oid}.jpg`                                                                                                                                                                                      |
| Directory                     | Flat `YYYYMMDD/` (không có `src_N/` subdir)                                                                                                                                                                                               |
| Message fields                | `event`, `pid`, `sid`, `sname`, `instance_key`, `oid`, `object_key`, `parent*`, `class`, `conf`, `labels`, `top/left/w/h`, `s_w_ff/s_h_ff/w_ff/h_ff`, `fname/fname_ff`, `event_ts`, `mid`, `prev_mid`, `pub_type`, `pub_reason`, `hb_seq` |
| `fname/fname_ff`              | Relative path từ `save_dir`                                                                                                                                                                                                               |
| `event_ts`                    | Unix epoch ms dưới dạng **string**                                                                                                                                                                                                        |
| Token bucket bypass           | `BURST_MAX=3`, `TOKEN_REFILL_NS=5s`                                                                                                                                                                                                       |
| Batch-accumulate-then-publish | Accumulate → `nvds_obj_enc_finish()` → publish                                                                                                                                                                                            |
| Full-frame dedup per batch    | Chỉ encode 1 lần mỗi `frame_num` mỗi batch                                                                                                                                                                                                |
| 5 state maps + stale cleanup  | `object_keys_`, `object_last_seen_`, `last_capture_pts_`, `pub_state_`, `child_parent_oid_cache_`                                                                                                                                         |
| Exit messages                 | Publish metadata-only khi stale cleanup xóa object                                                                                                                                                                                        |
| `mid` / `prev_mid` chain      | UUIDv7 message ID + prev_mid cho event correlation                                                                                                                                                                                        |
| cudaDeviceSynchronize         | Sync trước encode để tránh partial GPU buffer                                                                                                                                                                                             |

---

### 9.3 vms-engine Cải Thiện So Với lantanav2 V2

#### A. Redis Flat Publish — Không có `data` wrapper

```
// lantanav2 — XADD channel * data <json_string>
// Consumer nhận: {id: "...", data: "{\"event\":\"crop_bb\", ...}"}
// Phải parse lại JSON string lần nữa

// vms-engine — publish_json() flattens fields
// XADD channel * event crop_bb pid pipeline_01 sid 0 class car conf 0.92 ...
// Consumer nhận Redis hash trực tiếp: {id: "...", event: "crop_bb", class: "car", ...}
// Không cần parse thêm
```

Implemented qua `IMessageProducer::publish_json(channel, json_str)`: Redis implementation parse JSON → build `XADD argv[]` với tất cả fields phẳng. Kafka implementation delegate sang regular publish (JSON string is the payload).

---

#### B. Config System — YAML structs thay vì semicolon string

```cpp
// lantanav2 — parse lúc runtime, dễ lỗi, không có IDE support
init_context("save_dir=/path;labels=car,person;interval=5;quality=85;...");

// vms-engine — typed, validated, IDE completion
void configure(const PipelineConfig& config,
               const EventHandlerConfig& handler,
               IMessageProducer* producer);
// Truy cập: handler.save_dir, handler.image_quality, config.sources.width, ...
```

Lợi ích: compile-time type checking, no string parsing bugs, YAML validation tập trung.

#### C. Transport Abstraction — IMessageProducer

```cpp
// lantanav2 — coupled với Redis
std::unique_ptr<RedisStreamProducer> redis_producer_;
redis_producer_->send_message(fields);  // Chỉ dùng được Redis

// vms-engine — abstracted
IMessageProducer* producer_;
producer_->publish(channel_, json_str);  // Redis, Kafka, hoặc bất kỳ backend
```

Lợi ích: pipeline có thể switch sang Kafka mà không cần thay đổi handler.

#### D. JSON Native Types — Chính xác hơn cho consumer

```cpp
// lantanav2 — tất cả bbox/dims dưới dạng string
{"top", std::to_string(msg.rect_top)},   // "123.456789" — floating point noise
{"s_w_ff", std::to_string(msg.source_frame_width)},

// vms-engine — native JSON types
j["top"]    = static_cast<float>(left);   // 123.456  — proper float
j["s_w_ff"] = src_frame_w;               // 1920     — proper int
```

Consumer Python/JS nhận `float` và `int` thực sự thay vì phải parse string.

#### E. Payload Hash Dedup — Suppress duplicate heartbeat

```cpp
// lantanav2 V2 — track hash nhưng không suppress heartbeat dựa vào hash

// vms-engine — suppress heartbeat khi bbox/class không thay đổi
std::size_t new_hash = compute_payload_hash(class_id, label, left, top, w, h);
if (new_hash == state.last_payload_hash) {
    return;  // Suppress — object đứng yên, không publish redundant heartbeat
}
```

Lợi ích: giảm lượng message Redis đáng kể cho static scenes (camera nhìn bãi đỗ xe chẳng hạn).

#### F. Memory Stats + Emergency Hard Limit

```cpp
// vms-engine — log stats mỗi cycle + hard stop
void log_memory_stats() const;  // "object_keys=42, ~7KB"

static constexpr size_t MAX_TRACKED_OBJECTS = 5000;
if (max_map > MAX_TRACKED_OBJECTS) {
    object_keys_.clear(); object_last_seen_.clear(); // Emergency clear ALL maps
    last_capture_pts_.clear(); pub_state_.clear();
}
```

Lantanav2 không có safety net này → unbounded memory growth khi tracker churn cao.

#### G. Source Dims từ NvBufSurface

```cpp
// lantanav2 — lấy từ frame_meta (muxer processed dims)
source_frame_width = frame_meta->source_frame_width;

// vms-engine — lấy từ NvBufSurface (actual GPU buffer dims)
src_frame_w = static_cast<int>(ip_surf->surfaceList[frame_meta->batch_id].width);
src_frame_h = static_cast<int>(ip_surf->surfaceList[frame_meta->batch_id].height);
```

NvBufSurface phản ánh chính xác kích thước buffer GPU mà encoder thực sự đọc từ.

---

### 9.4 lantanav2 V2 có nhưng vms-engine KHÔNG port

#### A. Parent-Child Object Grouping

lantanav2 nhóm parent (PGIE) + children (SGIE) theo `GroupData` và xử lý theo thứ tự parent trước child.

vms-engine hiện tại cũng đã áp dụng group-based 2-pass tương tự (`ObjectGroup`):

- Pass 1: build groups theo `parent_tid`
- Pass 2: `process_object(parent)` rồi mới `process_object(children)`
- **Strict default**: nếu parent không publish trong frame đó (filter/throttle/dedup), toàn bộ children của group bị skip publish
- Có fallback cache `child_parent_oid_cache_` để giữ linkage khi `obj->parent` bị null sau tracker
- Child publish dùng `parent_tid_hint` từ group context, không phụ thuộc hoàn toàn vào con trỏ `obj->parent`

#### B. ChildPresenceStateV2 Hysteresis

```
lantanav2:
  FirstSeen ← khi children xuất hiện và đủ stable (hysteresis threshold)
  Exit      ← khi children biến mất và đủ stable
  ↕ Không fire khi child presence chưa stable (chống noise)

vms-engine:
  FirstSeen ← khi tracker ID chưa có trong object state (ngay lập tức)
  Exit      ← khi PTS không cập nhật quá stale_object_timeout_min
  Parent linkage SGIE vẫn được duy trì qua `child_parent_oid_cache_` + `parent_tid_hint`
```

Hysteresis của lantanav2 phù hợp với use case SGIE-driven (ví dụ: "vehicle has license plate"). vms-engine dùng PTS-based approach phù hợp với PGIE-only pipeline.

#### C. ChildPresenceStateV2 Hysteresis — Bypass Khi Child Presence Thay Đổi

lantanav2 track child presence (có/không có SGIE-detected child objects) qua các frame và áp dụng hysteresis threshold trước khi fire FirstSeen/Exit. vms-engine không có child-presence layer riêng — FirstSeen/Exit dựa trên tracker ID + PTS staleness, nhưng đã bổ sung parent-link fallback để ổn định SGIE child publish.

> **Lưu ý**: vms-engine đã implement **label-change bypass** tương đương `LabelStateV2` của lantanav2 thông qua `last_sgie_labels` tracking trong `ObjectPubState`. Bypass fires khi SGIE label string thay đổi giữa 2 lần check (cùng nguyên lý, không cần ChildPresence layer).

#### E. hb_seq — Conditional Field

lantanav2 chỉ push `hb_seq` vào Redis XADD khi `pub_type == Heartbeat`. vms-engine luôn include `hb_seq` trong JSON cho mọi pub_type — đơn giản hơn cho consumer (không cần check key existence).

#### F. Redis Connection Tracking Per-Message

lantanav2 kiểm tra `is_connected()` trước **và** sau mỗi lần send để track partial failure. vms-engine coi publish là fire-and-forget với LOG_W khi thất bại — simpler nhưng ít granular hơn.

---

## 10. ext_processor — Đã Implement

`ExternalProcessorService` được wired vào pipeline qua `CropObjectHandler::configure()`. Config struct:

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

**Runtime behavior** (đã implement — xem `ext_proc_svc.cpp`):

1. Sau khi crop JPEG encode xong, nếu object label match rule → encode in-memory JPEG (independent NvDsObjEnc context)
2. Launch detached thread → HTTP multipart POST đến endpoint
3. Parse JSON response theo `result_path` / `display_path`
4. Publish enrichment event (event=ext_proc) lên message broker
5. Rate-limit theo `min_interval_sec` per label per object (monotonic throttle)
6. Fail gracefully — timeout/error không ảnh hưởng pipeline
7. Shared_ptr giữ Impl alive qua thread lifetime (pipeline teardown safe)

> **Chi tiết architecture** → [`ext_proc_svc.md`](ext_proc_svc.md)

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
