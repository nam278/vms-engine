# 10. DeepStream REST API — Quản lý Stream Động

## 1. Tổng quan

`nvmultiurisrcbin` tích hợp sẵn một **HTTP server** (dựa trên CivetWeb) có tên `nvds_rest_server`. Server này cho phép thêm/bỏ camera **trong khi pipeline đang chạy** mà không cần restart.

```
[FastAPI / cURL]
      │
      │  POST /api/v1/stream/add
      │  POST /api/v1/stream/remove
      ▼
[CivetWeb — nvds_rest_server]  ←── nvmultiurisrcbin
      │                                     │
      │  camera_add / camera_remove          │
      ▼                                     ▼
[nvurisrcbin] ──────────────────► [nvstreammux]
(tạo / xóa source pad động)
```

### Đặc điểm kỹ thuật

| Attribute      | Value                                         |
| -------------- | --------------------------------------------- |
| HTTP Server    | CivetWeb (`nvds_rest_server`)                 |
| Protocol       | HTTP/1.1 (không có TLS)                       |
| Default port   | `9000`                                        |
| Bind address   | `0.0.0.0` (tất cả interfaces, không đổi được) |
| Configuration  | YAML field `rest_api_port` trong `sources:`   |
| Disable        | `rest_api_port: 0`                            |
| Available from | DeepStream 6.2+                               |

> ⚠️ **DS8 SIGSEGV Constraint**: Property `ip-address` trên `nvmultiurisrcbin` luôn gây **SIGSEGV** trong DeepStream 8.0 khi set qua `g_object_set`, bất kể giá trị. Do đó server luôn bind `0.0.0.0`. Không được thêm `ip_address` vào YAML hay C++ code.

---

## 2. Cấu hình trong YAML

```yaml
sources:
  type: nvmultiurisrcbin

  # REST API built-in (CivetWeb / nvds_rest_server)
  # 0 = disable hoàn toàn (mặc định, tránh port conflict)
  # >0 = bind trên port đó (DS default là 9000)
  # NOTE: ip_address KHÔNG thể config — DS8 SIGSEGV bug.
  rest_api_port: 9000 # bật REST API
  # rest_api_port: 0   # tắt REST API

  max_batch_size: 4
  mode: 0
  # ... các properties khác ...
```

### Lưu ý quan trọng về cấu hình

- `rest_api_port` là **integer** trong YAML, nhưng được convert sang **string** trước khi gọi `g_object_set` (GStreamer property `port` là type `string`)
- Không cần restart pipeline khi thêm/bỏ camera — REST API xử lý hoàn toàn dynamic
- `max_batch_size` phải ≥ tổng số camera tối đa bạn định dùng (gồm camera khởi động + camera add sau)

---

## 3. Các Endpoints

Base URL: `http://<host>:<rest_api_port>`

### 3.1 Thêm stream mới

```
POST /api/v1/stream/add
Content-Type: application/json
```

**Payload:**

```json
{
  "key": "sensor",
  "value": {
    "camera_id": "camera-03",
    "camera_name": "back_door",
    "camera_url": "rtsp://192.168.1.103:554/stream",
    "change": "camera_add",
    "metadata": {
      "resolution": "1920 x1080",
      "codec": "h264",
      "framerate": 25
    }
  },
  "headers": {
    "source": "vms_app",
    "created_at": "2025-01-15T08:00:00.000Z"
  }
}
```

**Mandatory fields** (các trường bắt buộc):

| Field              | Mô tả                                                                       |
| ------------------ | --------------------------------------------------------------------------- |
| `value.camera_id`  | ID duy nhất cho sensor/camera. Dùng để track và remove                      |
| `value.camera_url` | RTSP URI hoặc file URI                                                      |
| `value.change`     | **Phải chứa substring `"add"`** — e.g. `"camera_add"`, `"camera_streaming"` |

**Optional fields**: `camera_name`, `metadata`, `headers` — được accept nhưng phần lớn bị ignore bởi nvmultiurisrcbin.

**cURL example:**

```bash
curl -XPOST 'http://localhost:9000/api/v1/stream/add' \
  -H 'Content-Type: application/json' \
  -d '{
    "key": "sensor",
    "value": {
      "camera_id": "camera-03",
      "camera_url": "rtsp://192.168.1.103:554/stream",
      "change": "camera_add"
    }
  }'
```

### 3.2 Bỏ stream

```
POST /api/v1/stream/remove
Content-Type: application/json
```

**Payload:**

```json
{
  "key": "sensor",
  "value": {
    "camera_id": "camera-03",
    "camera_name": "back_door",
    "camera_url": "rtsp://192.168.1.103:554/stream",
    "change": "camera_remove",
    "metadata": {
      "resolution": "1920 x1080",
      "codec": "h264",
      "framerate": 25
    }
  }
}
```

**Mandatory fields**:

| Field              | Mô tả                                                       |
| ------------------ | ----------------------------------------------------------- |
| `value.camera_id`  | ID của camera muốn remove (phải match với lúc add)          |
| `value.camera_url` | URI (nên match với lúc add, dùng để xác định stream)        |
| `value.change`     | **Phải chứa substring `"remove"`** — e.g. `"camera_remove"` |

**cURL example:**

```bash
curl -XPOST 'http://localhost:9000/api/v1/stream/remove' \
  -H 'Content-Type: application/json' \
  -d '{
    "key": "sensor",
    "value": {
      "camera_id": "camera-03",
      "camera_url": "rtsp://192.168.1.103:554/stream",
      "change": "camera_remove"
    }
  }'
```

### 3.3 GET Stream Info (DS 7.0+)

```
GET /api/v1/stream/info
```

Returns list of currently active streams.

**cURL example:**

```bash
curl 'http://localhost:9000/api/v1/stream/info'
```

### 3.4 GET DeepStream Readiness (DS 8.0+)

```
GET /api/v1/stream/readiness
```

Returns whether the pipeline is ready to accept streams.

**cURL example:**

```bash
curl 'http://localhost:9000/api/v1/stream/readiness'
```

---

## 4. Hành vi quan trọng từ DeepStream docs

### 4.1 Quy tắc `change` field

> **Từ NVIDIA docs**: `value.change` must contain substring `"add"` or `"remove"` respectively.

```
 "camera_add"        → ✅ add stream
 "camera_streaming"  → ✅ add stream (chứa...không, cái này đặc biệt)
 "camera_remove"     → ✅ remove stream
 "add_camera"        → ✅ add stream (chứa "add")
 "remove_camera"     → ✅ remove stream (chứa "remove")
 "update"            → ❌ không match — bị ignore
```

### 4.2 Pad reuse

Khi một stream bị remove và sau đó một stream mới được add:

- Pad ID cũ được **reuse** — không tạo pad mới
- Giúp pipeline không bị thay đổi topology

### 4.3 `drop-pipeline-eos` bắt buộc

```yaml
sources:
  drop_pipeline_eos: true # BẮT BUỘC khi dùng dynamic add/remove
```

Nếu không set `drop_pipeline_eos: true`, khi camera cuối cùng bị remove (EOS), pipeline sẽ tắt hoàn toàn và không nhận add request tiếp theo.

### 4.4 `max_batch_size` giới hạn số camera tối đa

```yaml
sources:
  max_batch_size: 8 # tối đa 8 camera cùng lúc (kể cả camera static + dynamic)
```

Sau khi đạt `max_batch_size`, request `/stream/add` tiếp theo sẽ bị từ chối.

---

## 5. Tích hợp với FastAPI Backend (`vms_app_fastapi`)

Khi `vms_app_fastapi` nhận yêu cầu thêm/bỏ camera, nó forward đến REST API của vms-engine:

```python
import httpx

DEEPSTREAM_REST_BASE = "http://vms-engine-host:9000"

async def add_camera_to_pipeline(camera_id: str, camera_url: str) -> bool:
    payload = {
        "key": "sensor",
        "value": {
            "camera_id": camera_id,
            "camera_url": camera_url,
            "change": "camera_add",
        }
    }
    async with httpx.AsyncClient() as client:
        response = await client.post(
            f"{DEEPSTREAM_REST_BASE}/api/v1/stream/add",
            json=payload,
            timeout=10.0
        )
    return response.status_code == 200


async def remove_camera_from_pipeline(camera_id: str, camera_url: str) -> bool:
    payload = {
        "key": "sensor",
        "value": {
            "camera_id": camera_id,
            "camera_url": camera_url,
            "change": "camera_remove",
        }
    }
    async with httpx.AsyncClient() as client:
        response = await client.post(
            f"{DEEPSTREAM_REST_BASE}/api/v1/stream/remove",
            json=payload,
            timeout=10.0
        )
    return response.status_code == 200
```

### Sơ đồ luồng đầy đủ

```
[User/Dashboard]
      │
      │  REST call: POST /api/v1/cameras
      ▼
[vms_app_fastapi]
      │  1. Persist camera record to PostgreSQL
      │  2. Forward to vms-engine REST API
      ▼
[vms-engine: CivetWeb :9000]
      │  POST /api/v1/stream/add
      ▼
[nvmultiurisrcbin]
      │  Dynamically create nvurisrcbin + pad
      ▼
[nvstreammux → nvinfer → nvtracker → ...]
```

---

## 6. Xử lý lỗi

### Port đã bị chiếm

```
cannot bind to 9000: 98 (Address already in use)
CivetException caught: null context when constructing CivetServer
```

**Nguyên nhân**: Port đã được process khác sử dụng (ví dụ: nhiều container chạy trên cùng host network).

**Fix**: Đổi port hoặc disable:

```yaml
# Option 1: Đổi port
rest_api_port: 9001

# Option 2: Disable hoàn toàn (không cần dynamic add/remove)
rest_api_port: 0
```

### Kiểm tra port đang lắng nghe

```bash
# Trong container
ss -tlnp | grep 9000

# Từ host
curl -s 'http://localhost:9000/api/v1/stream/readiness'
```

### Response codes

| HTTP Status | Ý nghĩa                                          |
| ----------- | ------------------------------------------------ |
| `200 OK`    | Request được accept và stream đã được add/remove |
| `4xx`       | Payload không hợp lệ (thiếu mandatory fields)    |
| `5xx`       | Internal DeepStream error                        |

> **Note**: nvmultiurisrcbin REST API không trả về body JSON chi tiết — chỉ có status code là quan trọng.

---

## 7. So sánh static sources vs dynamic add

| Approach        | Khi nào dùng                            | Cách cấu hình                             |
| --------------- | --------------------------------------- | ----------------------------------------- |
| **Static list** | Đã biết trước toàn bộ cameras khi start | Liệt kê trong `cameras:` section của YAML |
| **Dynamic add** | Cameras thay đổi lúc runtime            | Dùng REST API `/api/v1/stream/add`        |
| **Kết hợp**     | Start với 1–2 cameras, thêm sau         | YAML + REST API — cả hai đều work         |

Static cameras được add tự động qua `uri-list` property khi pipeline start. REST API add thêm on top.

---

## 8. Debug

```bash
# Xem DeepStream REST server logs
GST_DEBUG="nvmultiurisrcbin:4" ./build/bin/vms_engine -c configs/default.yml

# Kiểm tra port
ss -tlnp | grep 9000

# Test add stream
curl -v -XPOST 'http://localhost:9000/api/v1/stream/add' \
  -H 'Content-Type: application/json' \
  -d '{"key":"sensor","value":{"camera_id":"test01","camera_url":"rtsp://...","change":"camera_add"}}'

# Test readiness (DS 8.0)
curl 'http://localhost:9000/api/v1/stream/readiness'
```

---

## 9. Tham khảo

- [NVIDIA DeepStream — Gst-nvmultiurisrcbin](https://docs.nvidia.com/metropolis/deepstream/dev-guide/text/DS_plugin_gst-nvmultiurisrcbin.html)
- [nvds_rest_server source](https://docs.nvidia.com/metropolis/deepstream/dev-guide/text/DS_RestServer.html)
- [`source_builder.cpp`](../../../pipeline/src/builders/source_builder.cpp) — trong vms-engine
- [`config_types.hpp`](../../../core/include/engine/core/config/config_types.hpp) — `SourcesConfig.rest_api_port`
- [`dev/configs/default.yml`](../../../dev/configs/default.yml) — runtime config
