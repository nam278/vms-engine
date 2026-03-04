# External Processor Service (ExtProcSvc)

## 1. Tổng Quan

`ExternalProcessorService` là một standalone service thực hiện **HTTP-based AI enrichment** cho từng detected object — điển hình là nhận dạng khuôn mặt (face recognition), tra cứu biển số xe, phân loại tuỳ chỉnh, v.v.

Service được **sở hữu bởi** `CropObjectHandler` và được gọi bên trong pad-probe callback khi `NvBufSurface` vẫn còn được GPU-map.

### Luồng xử lý tổng quan

```
CropObjectHandler (pad probe, tracker:src)
    │
    ├── Per-object (inside batch loop, surface still mapped)
    │       │
    │       └── ExternalProcessorService::process_object()
    │               │
    │               ├─ 1. Label lookup → ExtProcessorRule
    │               ├─ 2. Throttle check  (source:tracker_id:label)
    │               ├─ 3. encode_object_jpeg()
    │               │       └── nvds_obj_enc_process()  [saveImg=FALSE]
    │               │       └── nvds_obj_enc_finish()   [per-object, own ctx]
    │               │       └── read NVDS_CROP_IMAGE_META → JPEG bytes
    │               │
    │               └─ 4. std::thread::detach()
    │                       │
    │                       ├── CURL multipart POST → endpoint
    │                       ├── Parse JSON response (dot-path)
    │                       └── producer_->publish(channel, json.dump())
    │
    └── (loop tiếp tục, sau cùng gọi nvds_obj_enc_finish() cho file encoder)
```

### Điểm khác biệt với lantanav2 ExternalProcessingServiceV2

| Khía cạnh            | lantanav2 V2                                                   | vms-engine ExtProcSvc                                                   |
| -------------------- | -------------------------------------------------------------- | ----------------------------------------------------------------------- |
| Config lookup        | `unordered_map<string, ApiProcessingRuleV2>` (build from YAML) | `vector<ExtProcessorRule>` → build internal `rule_map` in `configure()` |
| Pipeline clock       | `GstClock*` (optional)                                         | `std::chrono::steady_clock` trực tiếp                                   |
| OSD support          | Có (`pending_osd_results_`, `TextMeta`)                        | **Không** — vms-engine không dùng nvdsosd                               |
| Parent/child object  | Throttle key = `parent_id:label`                               | Throttle key = `source_id:tracker_id:label`                             |
| Messaging interface  | `RedisStreamProducer*` (concrete)                              | `IMessageProducer*` (interface, borrowed)                               |
| Publish format       | Redis XADD multi-field                                         | `producer_->publish(channel, json.dump())`                              |
| Impl lifetime safety | Raw pointer capture                                            | `shared_ptr<Impl>` capture trong detached thread                        |

---

## 2. Cấu Hình YAML

Cấu hình `ext_processor` là **sub-block** bên trong `event_handlers` entry có `trigger: crop_objects`:

```yaml
event_handlers:
  - id: crop_objects
    enable: true
    type: on_detect
    probe_element: tracker
    trigger: crop_objects
    label_filter:
      - face
      - person
    save_dir: "/opt/vms_engine/dev/rec/objects"
    capture_interval_sec: 5
    image_quality: 85
    channel: "crop_objects" # Redis Stream / Kafka topic

    ext_processor:
      enable: true
      min_interval_sec:
        5 # Throttle: đợi ít nhất N giây trước khi gọi lại API
        # cùng (source_id, tracker_id, label)
      rules:
        - label: face # Chỉ xử lý object có label này
          endpoint: "http://face-rec-svc:8080/api/recognize"
          result_path: "match.external_id" # Dot-notation JSON path → "labels" field
          display_path: "match.face_name" # Dot-notation JSON path → "display" field
          params: # Optional query parameters
            threshold: "0.7"
            max_results: "1"

        - label: license_plate
          endpoint: "http://lpr-svc:9090/api/recognize"
          result_path: "plate.number"
          display_path: "plate.owner"
          params: {}
```

### Các trường cấu hình

| Trường YAML            | Kiểu                  | Mô tả                                                              |
| ---------------------- | --------------------- | ------------------------------------------------------------------ |
| `enable`               | `bool`                | Bật/tắt toàn bộ service                                            |
| `min_interval_sec`     | `int`                 | Throttle: khoảng cách tối thiểu (giây) giữa 2 API call cùng object |
| `rules[].label`        | `string`              | Label của object để áp dụng rule (e.g. `"face"`)                   |
| `rules[].endpoint`     | `string`              | URL endpoint nhận HTTP POST multipart/form-data                    |
| `rules[].result_path`  | `string`              | Dot-notation path vào JSON response để lấy kết quả chính           |
| `rules[].display_path` | `string`              | Dot-notation path vào JSON response để lấy text hiển thị           |
| `rules[].params`       | `map<string, string>` | Query parameters thêm vào URL (URL-escaped tự động)                |

---

## 3. Cấu Trúc Code

```
pipeline/include/engine/pipeline/probes/ext_proc_svc.hpp   ← Public interface
pipeline/src/probes/ext_proc_svc.cpp                       ← Implementation (Impl pimpl)
```

### Class diagram

```
ExternalProcessorService
├── configure(config, pipeline_id, producer*, channel)
├── process_object(obj_meta*, frame_meta*, batch_surf*, ...)
└── pimpl_: shared_ptr<Impl>
        ├── config: ExtProcessorConfig
        ├── rule_map: unordered_map<string, ExtProcessorRule*>
        ├── obj_enc_ctx: NvDsObjEncCtxHandle  ← RIÊNG, không share với CropObjectHandler
        ├── throttle_mutex + last_processed_ns: unordered_map
        ├── init_encoder() / destroy_encoder()
        ├── should_process(key) → bool
        ├── encode_object_jpeg(obj*, frame*, surf*) → vector<uchar>
        └── perform_api_call(jpeg_data, rule*, metadata...) [runs in detached thread]
```

### Pimpl với `shared_ptr`

Impl được wrap bởi `shared_ptr` thay vì `unique_ptr` thông thường để đảm bảo **lifetime safety** trong detached threads:

```cpp
// ExternalProcessorService::process_object()
auto impl_ref = pimpl_;  // copy shared_ptr — extends Impl lifetime into thread

std::thread([impl_ref = std::move(impl_ref), jpeg = std::move(jpeg_data), ...] {
    impl_ref->perform_api_call(...);  // Impl còn sống dù service bị destroy
}).detach();
```

Nếu `ExternalProcessorService` bị destroy trong khi thread đang chạy (trường hợp pipeline teardown race), `Impl` vẫn alive đủ lâu để API call hoàn thành mà không crash.

---

## 4. Cơ Chế Hoạt Động Chi Tiết

### 4.1 Throttle (Per-object Rate Limiting)

Throttle key được tạo từ 3 thành phần để tránh flood API:

```
key = "<source_id>:<tracker_id>:<label>"
e.g. "0:42:face"
```

Kiểm tra `last_processed_ns[key]`. Nếu `now - last < min_interval_sec * 1e9 ns` → skip.
Throttle dùng `std::chrono::steady_clock` (monotonic, không bị ảnh hưởng bởi system clock adjustment).

### 4.2 In-Memory JPEG Encoding

Service dùng **encoder context riêng** (`obj_enc_ctx`), độc lập với `enc_ctx_` của `CropObjectHandler` (dùng cho file saving). Hai context hoạt động song song trên cùng `NvBufSurface` — an toàn vì đều chỉ đọc (read-only surface access).

```cpp
NvDsObjEncUsrArgs enc_args{};
enc_args.saveImg       = FALSE;    // Không ghi file
enc_args.attachUsrMeta = TRUE;     // Attach JPEG bytes vào obj_meta->obj_user_meta_list
enc_args.quality       = 80;

nvds_obj_enc_process(obj_enc_ctx, &enc_args, batch_surf, obj_meta, frame_meta);
nvds_obj_enc_finish(obj_enc_ctx);  // Block until encoded — gọi per-object, không tích lũy

// Read encoded bytes
for (auto* ul = obj_meta->obj_user_meta_list; ul; ul = ul->next) {
    auto* um = (NvDsUserMeta*)ul->data;
    if (um->base_meta.meta_type == NVDS_CROP_IMAGE_META) {
        auto* enc_out = (NvDsObjEncOutParams*)um->user_meta_data;
        jpeg_data = vector<uchar>(enc_out->outBuffer, enc_out->outBuffer + enc_out->outLen);
    }
}
```

> ⚠️ **Timing constraint**: `encode_object_jpeg()` PHẢI được gọi trong khi `ip_surf` còn được map (trước khi `NvBufSurfaceUnmap`). Do đó `process_object()` được gọi **bên trong vòng lặp object** của `CropObjectHandler::process_batch()`, trước khi vòng lặp kết thúc và `nvds_obj_enc_finish(enc_ctx_)` được gọi.

### 4.3 HTTP POST (CURL Multipart)

```
POST http://endpoint?param1=val1&param2=val2
Content-Type: multipart/form-data; boundary=...

--boundary
Content-Disposition: form-data; name="file"; filename="image.jpg"
Content-Type: image/jpeg

<JPEG bytes>
--boundary--
```

- Field name: `"file"` (cố định, tương thích lantanav2)
- Filename: `"image.jpg"`
- Timeout: connect=5s, total=10s
- User-Agent: `ExtProcSvc/1.0`

### 4.4 JSON Response Parsing

Response body được parse bằng `nlohmann::json`. `result_path` và `display_path` là **dot-notation** paths:

```
"match.external_id"  →  json["match"]["external_id"]
"data.face.name"     →  json["data"]["face"]["name"]
```

Nếu path không tồn tại hoặc JSON parse thất bại → publish với `result=""`, `display=""`.

---

## 5. Cấu Trúc Message Publish

Message được publish bằng `producer_->publish(channel, json.dump())` đến cùng channel với `CropObjectHandler` (chỉ định qua `broker.channel`).

### JSON Schema (event: "ext_proc")

```json
{
  "event": "ext_proc",
  "pid": "pipeline-01",
  "sid": 0,
  "sname": "camera-01",
  "instance_key": "01956abc-...",
  "oid": 42,
  "object_key": "01956def-...",
  "parent_object_key": "",
  "parent": "",
  "parent_instance_key": "",
  "class": "face",
  "class_id": 0,
  "conf": 0.92,
  "labels": "emp_001|Le Van A",
  "result": "emp_001",
  "display": "Le Van A",
  "top": "",
  "left": "",
  "w": "",
  "h": "",
  "s_w_ff": "",
  "s_h_ff": "",
  "w_ff": "",
  "h_ff": "",
  "fname": "",
  "fname_ff": "",
  "event_ts": "1735825200000"
}
```

### Giải thích các trường

| Trường             | Nguồn                                                      |
| ------------------ | ---------------------------------------------------------- |
| `event`            | Constant `"ext_proc"`                                      |
| `pid`              | `config.pipeline.id`                                       |
| `sid`              | `frame_meta->source_id`                                    |
| `sname`            | Camera name từ `source_id_to_name_` map                    |
| `instance_key`     | UUID per detection instance (từ `CropObjectHandler`)       |
| `oid`              | `obj_meta->object_id` (tracker ID)                         |
| `object_key`       | UUID persistent per tracked object                         |
| `class`            | `obj_meta->obj_label`                                      |
| `class_id`         | `obj_meta->class_id`                                       |
| `conf`             | `obj_meta->confidence`                                     |
| `labels`           | `result \| display` (lantanav2-compatible composite field) |
| `result`           | Giá trị parse từ `result_path` trong JSON response         |
| `display`          | Giá trị parse từ `display_path` trong JSON response        |
| `top/left/w/h/...` | Empty — ảnh gửi inline qua HTTP, không lưu file            |
| `event_ts`         | `std::chrono::system_clock` epoch ms (string)              |

> **Tương thích lantanav2**: Cấu trúc JSON này giữ nguyên tất cả các field từ lantanav2 `ExternalProcessingServiceV2` Redis XADD format. Downstream consumers (FastAPI workers) không cần thay đổi.

---

## 6. Threading Model & Lifecycle

### Non-blocking Design

```
pad probe callback (GStreamer streaming thread)
    │
    └── process_object()    ← nhanh: rule check, throttle, encode (~1-5ms)
            │
            └── std::thread::detach()
                    │
                    └── perform_api_call()   ← slow: HTTP (~50-500ms), JSON parse, publish
                                                     chạy trong background thread
```

Streaming thread **không bị block** bởi HTTP latency. Không có back-pressure — nếu nhiều objects đủ điều kiện trong cùng batch, nhiều threads được sinh ra đồng thời. Throttle là cơ chế duy nhất giới hạn concurrency.

### Destructor Safety

```cpp
ExternalProcessorService::~ExternalProcessorService() {
    pimpl_->destroy_encoder();  // Release NvDsObjEncCtxHandle
    // pimpl_ shared_ptr giảm ref count; background threads vẫn giữ copies
    // → Impl được destroy sau khi thread cuối cùng kết thúc
}
```

### Object Ownership

| Resource              | Owner                      | Lifecycle                                                            |
| --------------------- | -------------------------- | -------------------------------------------------------------------- |
| `NvDsObjEncCtxHandle` | `Impl`                     | Created in `configure()`, destroyed in `~ExternalProcessorService()` |
| `IMessageProducer*`   | **Không owned** (borrowed) | Sống đến khi pipeline destroyed                                      |
| `ExtProcessorRule*`   | `Impl::config`             | Stable within Impl lifetime                                          |
| JPEG byte vector      | Moved into lambda          | Owned by detached thread lambda                                      |

---

## 7. Tích Hợp với CropObjectHandler

### Điểm tích hợp trong `process_batch()`

```
process_batch(buf)
  │
  ├── Map NvBufSurface
  ├── For each frame:
  │     For each object:
  │       ├── decide_capture()
  │       ├── nvds_obj_enc_process(enc_ctx_, ...)    ← file saving encoder
  │       ├── accumulate PendingMessage
  │       └── ext_proc_svc_->process_object(...)     ← ← ← TẠI ĐÂY
  │                   (ip_surf vẫn còn mapped)
  │
  ├── nvds_obj_enc_finish(enc_ctx_)   ← flush file encoder
  ├── NvBufSurfaceUnmap
  └── publish_pending_messages()
```

### configure() integration

```cpp
// Trong CropObjectHandler::configure()
ext_proc_svc_.reset();
if (handler.ext_processor
    && handler.ext_processor->enable
    && !handler.ext_processor->rules.empty()) {
    ext_proc_svc_ = std::make_unique<ExternalProcessorService>();
    ext_proc_svc_->configure(
        *handler.ext_processor,
        pipeline_id_,
        producer,       // Shared với CropObjectHandler
        broker_channel_ // Cùng Redis channel
    );
}
```

---

## 8. Ví Dụ Sử Dụng — Face Recognition

### Cấu hình hoàn chỉnh

```yaml
event_handlers:
  - id: crop_objects
    enable: true
    type: on_detect
    probe_element: tracker
    trigger: crop_objects
    label_filter: [face]
    save_dir: "/opt/vms_engine/dev/rec/objects"
    capture_interval_sec: 3
    image_quality: 90
    save_full_frame: false
    channel: "vms:detections" # Redis Stream / Kafka topic

    ext_processor:
      enable: true
      min_interval_sec: 5
      rules:
        - label: face
          endpoint: "http://face-rec-svc:8080/api/v1/recognize"
          result_path: "data.person_id"
          display_path: "data.person_name"
          params:
            confidence_threshold: "0.65"
```

### Kịch bản hoạt động

1. `CropObjectHandler` detect object `label=face`, `tracker_id=42`
2. Throttle check: `key="0:42:face"` — lần đầu → cho qua
3. `encode_object_jpeg()` → JPEG 120KB
4. Thread detached → POST `http://face-rec-svc:8080/api/v1/recognize?confidence_threshold=0.65`
5. Face service trả về: `{"data":{"person_id":"emp_001","person_name":"Le Van A"}}`
6. Parse: `result="emp_001"`, `display="Le Van A"`
7. Publish đến `vms:detections`:
   ```json
   {
     "event": "ext_proc",
     "pid": "pipeline-01",
     "sid": 0,
     "sname": "camera-entrance",
     "class": "face",
     "labels": "emp_001|Le Van A",
     "result": "emp_001",
     "display": "Le Van A",
     "event_ts": "..."
   }
   ```

---

## 9. Lưu Ý Vận Hành

### Performance

- **CPU**: Mỗi API call = 1 OS thread. Với nhiều cameras + nhiều faces → nhiều threads đồng thời. Tăng `min_interval_sec` nếu bị overload.
- **Network**: HTTP timeout tổng = 10s. Nếu service chậm hơn, thread tồn tại lâu hơn. Cân nhắc retry strategy ở tầng application.
- **JPEG Encode**: ~1-3ms/object trên GPU. Encoder context riêng (`obj_enc_ctx`) không block `enc_ctx_` của file saving.

### Debugging

```bash
# Xem ext_proc calls (DEBUG level)
GST_DEBUG=2 ./build/bin/vms_engine -c configs/default.yml 2>&1 | grep ext_proc

# Xem throttle
# (LOG_T = TRACE level — set log_level: trace trong YAML)
```

### Tương thích phiên bản

| Thành phần    | Yêu cầu                                 |
| ------------- | --------------------------------------- |
| DeepStream    | 8.0 (NvDsObjEnc API v2)                 |
| libcurl       | 7.x+ (curl_mime API)                    |
| nlohmann/json | 3.11+ (đã fetch via CMake)              |
| C++           | C++17 (structured bindings, shared_ptr) |
