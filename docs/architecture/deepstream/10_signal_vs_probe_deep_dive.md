# 10. Signal vs Pad Probe — Khi nào dùng cái nào?

## 1. Tổng quan

Đây là câu hỏi quan trọng nhất khi extend vms-engine với custom logic. Chọn sai cơ chế có thể gây **performance issue** hoặc **data race**.

```
┌──────────────────────────────────────────────────────────────────┐
│                   PROCESSING THREAD (GStreamer)                   │
│                                                                  │
│  Source ──► PGIE ──► Tracker ──► Demux ──► OSD ──► Encoder ──►  │
│              │                                │                  │
│              ▼ (src pad)                      ▼ (appsink)         │
│         [PAD PROBE] ◄── runs synchronously    [SIGNAL] ◄── callback │
│                          on pipeline thread     on pipeline thread │
└──────────────────────────────────────────────────────────────────┘
```

## 2. GStreamer Signals

### Cách hoạt động

Signal là **GObject signal system** — callback được gọi khi một sự kiện xảy ra trên GObject (element). Callback chạy trên **thread của caller** (thường là pipeline streaming thread, trừ `async-handling = true`).

```cpp
// Kết nối signal
gulong signal_id = g_signal_connect(
    G_OBJECT(element),
    "signal-name",          // Tên signal từ element API
    G_CALLBACK(callback_fn),
    user_data);

// Ngắt kết nối
g_signal_handler_disconnect(G_OBJECT(element), signal_id);
```

### Khi nào dùng Signal

| Tình huống | Signal Name | Element |
|-----------|-------------|---------|
| Nhận frame data từ pipeline | `"new-sample"` | `appsink` |
| Smart record file hoàn tất | `"sr-done"` | `nvmultiurisrcbin` |
| Camera stream thêm/bỏ | `"pad-added"`, `"pad-removed"` | `nvstreamdemux` |
| Property thay đổi | `"notify::property-name"` | Bất kỳ GObject |
| Deep neural network output | `"new-inference-result"` | `nvinferserver` |

### appsink Pattern

`appsink` là cách phổ biến nhất để access frame data qua signal:

```cpp
// Cấu hình appsink
g_object_set(G_OBJECT(appsink),
    "emit-signals",   TRUE,    // Bật signal emission
    "max-buffers",    4,        // Drop old frames nếu queue đầy
    "drop",           TRUE,
    "sync",           FALSE,
    nullptr);

// Connect signal
g_signal_connect(appsink, "new-sample",
    G_CALLBACK([](GstElement* sink, gpointer data) -> GstFlowReturn {
        // Pull sample
        GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
        if (!sample) return GST_FLOW_EOS;

        // Process...
        GstBuffer* buf = gst_sample_get_buffer(sample);
        NvDsBatchMeta* meta = gst_buffer_get_nvds_batch_meta(buf);

        // LUÔN unref sau khi xong!
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }), user_data);
```

### sr-done Signal Pattern

```cpp
// Signal khi smart record file hoàn tất
g_signal_connect(
    G_OBJECT(src_element),
    "sr-done",
    G_CALLBACK([](GstElement* elem,
                  NvDsSRRecordingInfo* info,
                  gpointer data) {
        LOG_I("Recording done: file={}, stream={}, duration={}s",
              info->filename, info->source_id, info->duration);
    }),
    user_data);

// NvDsSRRecordingInfo fields:
// - filename: absolute path của file đã ghi
// - source_id: stream index
// - duration: seconds
// - file_size: bytes
// - session_id: NvDsSRSessionId
```

## 3. GStreamer Pad Probes

### Cách hoạt động

Pad probe là **callback được insert vào pad pipeline**. Callback chạy **synchronously** khi buffer đi qua pad đó — trên pipeline streaming thread.

```cpp
// Attach probe
gulong probe_id = gst_pad_add_probe(
    pad,
    GST_PAD_PROBE_TYPE_BUFFER,   // Probe type
    [](GstPad* pad, GstPadProbeInfo* info, gpointer data) -> GstPadProbeReturn {
        // ...
        return GST_PAD_PROBE_OK;
    },
    user_data,
    nullptr);   // GDestroyNotify

// Remove probe
gst_pad_remove_probe(pad, probe_id);
```

### Probe Types

```cpp
enum GstPadProbeType {
    GST_PAD_PROBE_TYPE_BUFFER,           // Inspect/modify/drop buffers
    GST_PAD_PROBE_TYPE_BUFFER_LIST,      // Inspect/modify buffer lists
    GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, // React to downstream events (EOS, flush)
    GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,   // React to upstream events (seek)
    GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM, // Intercept queries
    GST_PAD_PROBE_TYPE_IDLE,             // Probe khi pad idle (safe resize)
};
```

### Probe Return Values

```cpp
enum GstPadProbeReturn {
    GST_PAD_PROBE_OK,       // Cho buffer/event đi qua (bình thường)
    GST_PAD_PROBE_DROP,     // Drop buffer/event (không forward downstream)
    GST_PAD_PROBE_HANDLED,  // Đã xử lý hoàn toàn, dừng probe chain
    GST_PAD_PROBE_REMOVE,   // Remove probe sau lần này
    GST_PAD_PROBE_PASS,     // Tiếp tục với probe type khác
};
```

### Khi nào dùng Pad Probe

| Tình huống | Probe Type | Return Value |
|-----------|------------|-------------|
| Đọc NvDs metadata không modify | `BUFFER` | `OK` |
| Inject thêm metadata | `BUFFER` | `OK` |
| Drop frames theo điều kiện | `BUFFER` | `DROP` |
| Offset class IDs (SGIE) | `BUFFER` | `OK` |
| Trigger smart record auto | `BUFFER` | `OK` |
| Crop objects (avoid appsink) | `BUFFER` | `OK` |
| Resize buffer dynamically | `IDLE` | `OK` |

## 4. So sánh đầy đủ

| Tiêu chí | Signal (appsink) | Pad Probe |
|----------|-----------------|-----------|
| **Overhead** | Trung bình (copy buffer reference) | Thấp (inline callback) |
| **Thread** | Pipeline thread (hoặc async) | Pipeline thread (luôn luôn) |
| **Modify buffer** | Không thể (reference copy) | Có thể (direct access) |
| **Drop buffer** | Không | Có (`GST_PAD_PROBE_DROP`) |
| **Dễ implement** | Cao (sample API) | Vừa (pad probe API) |
| **Vị trí trong pipeline** | Cuối (sau encode thường) | Bất kỳ pad nào |
| **Blocking risk** | Cao nếu heavy processing | Cao — muốn async thì thread pool |

## 5. ⚠️ Threading Rules — Quan trọng

### Rule 1: Không block pipeline thread

Cả signal callback và pad probe đều chạy **trên pipeline streaming thread**. Bất kỳ blocking operation nào (disk I/O, network, mutex wait) sẽ **drop frames** hoặc gây **latency spike**.

```cpp
// ❌ SAI — Block pipeline thread
GstFlowReturn on_new_sample(GstElement* sink, gpointer) {
    auto* jpeg = encode_to_jpeg(sample);       // 50ms — quá lâu!
    storage.upload_to_s3(jpeg);               // 200ms — critical drop!
    return GST_FLOW_OK;
}

// ✅ ĐÚNG — Async processing
GstFlowReturn on_new_sample(GstElement* sink, gpointer) {
    auto sample_ref = gst_sample_ref(gst_app_sink_pull_sample(GST_APP_SINK(sink)));
    thread_pool_.submit([sample_ref] {
        // Process trong background thread
        auto* jpeg = encode_jpeg(sample_ref);
        storage.upload_async(jpeg);
        gst_sample_unref(sample_ref);
    });
    return GST_FLOW_OK;  // Return ngay lập tức
}
```

### Rule 2: Pad probe phải cực kỳ nhanh

```cpp
// ❌ SAI — Probe quá slow
GstPadProbeReturn my_probe(GstPad*, GstPadProbeInfo* info, gpointer) {
    auto* meta = gst_buffer_get_nvds_batch_meta(buf);
    for (auto* fl = ...) {
        for (auto* ol = ...) {
            http_client.post("/api/detect", serialize_object(ol)); // BLOCKING!
        }
    }
    return GST_PAD_PROBE_OK;
}

// ✅ ĐÚNG — Enqueue và return ngay
GstPadProbeReturn my_probe(GstPad*, GstPadProbeInfo* info, gpointer data) {
    auto* self = static_cast<MyProbeHandler*>(data);
    auto* buf = GST_PAD_PROBE_INFO_BUFFER(info);
    auto* meta = gst_buffer_get_nvds_batch_meta(buf);

    // Lightweight extract
    auto events = self->extract_events(meta);

    // Push to lock-free queue
    for (auto& event : events) {
        self->event_queue_.push(std::move(event));
    }
    return GST_PAD_PROBE_OK;  // Return ngay sau ~0.1ms
}
```

### Rule 3: NvDs metadata — KHÔNG FREE

```cpp
// ❌ SAI — CRASH!
auto* frame_meta = static_cast<NvDsFrameMeta*>(fl->data);
nvds_release_meta_lock(batch_meta);  // DON'T!
g_free(frame_meta);                   // DON'T!

// ✅ ĐÚNG — Đọc metadata nhưng không giải phóng
auto* frame_meta = static_cast<NvDsFrameMeta*>(fl->data);
// Chỉ đọc, không free — pipeline sở hữu metadata này
```

## 6. Quyết định nhanh

```
Cần làm gì?
│
├── Đọc NvDs metadata (object positions, class IDs, confidence)
│   └── Dùng: PAD PROBE trên src pad của nvinfer hoặc nvtracker
│
├── Lấy frame JPEG/PNG để crop & save
│   ├── Nhiều streams, low latency → PAD PROBE
│   └── Ít streams, code đơn giản hơn → APPSINK signal
│
├── React khi smart record file xong
│   └── Dùng: SIGNAL "sr-done" trên nvmultiurisrcbin
│
├── Drop specific objects/frames
│   └── Dùng: PAD PROBE với return GST_PAD_PROBE_DROP
│
├── Inject custom metadata
│   └── Dùng: PAD PROBE (modify metadata in-place)
│
├── Link elements dynamically (stream add/remove)
│   └── Dùng: SIGNAL "pad-added" trên nvstreamdemux
│
└── Gửi kết quả ra external service (HTTP, Redis, ...)
    └── Dùng: PAD PROBE + ThreadPool (async queue)
```

## 7. Ví dụ: Hybrid Approach (Probe + Signal)

Trong nhiều trường hợp thực tế, dùng **cả hai**:

```cpp
// Pattern thực tiễn: Smart Record tự động triggered bởi AI detection
// 1. Probe: detect trigger condition (nhanh, trực tiếp từ AI output)
// 2. Signal: handle recording completion (async, upload)

class AutoSmartRecordSystem {
public:
    void setup(GstElement* pipeline) {
        // === PAD PROBE: Phát hiện object kích hoạt recording ===
        GstElement* tracker = gst_bin_get_by_name(GST_BIN(pipeline), "nvtracker");
        probe_pad_.reset(gst_element_get_static_pad(tracker, "src"), gst_object_unref);

        probe_id_ = gst_pad_add_probe(
            probe_pad_.get(),
            GST_PAD_PROBE_TYPE_BUFFER,
            [](GstPad*, GstPadProbeInfo* info, gpointer d) {
                return static_cast<AutoSmartRecordSystem*>(d)->detection_probe(info);
            }, this, nullptr);

        // === SIGNAL: Nhận file path khi recording xong ===
        GstElement* src = gst_bin_get_by_name(GST_BIN(pipeline), "src_muxer");
        g_signal_connect(src, "sr-done",
            G_CALLBACK(on_sr_done_static), this);
    }

private:
    GstPadProbeReturn detection_probe(GstPadProbeInfo* info) {
        auto* meta = gst_buffer_get_nvds_batch_meta(
            GST_PAD_PROBE_INFO_BUFFER(info));

        for (auto* fl = meta->frame_meta_list; fl; fl = fl->next) {
            auto* frame = static_cast<NvDsFrameMeta*>(fl->data);
            for (auto* ol = frame->obj_meta_list; ol; ol = ol->next) {
                auto* obj = static_cast<NvDsObjectMeta*>(ol->data);
                if (obj->class_id == CLASS_INTRUDER && obj->confidence > 0.8f) {
                    // Enqueue recording request (không block)
                    recording_queue_.push({frame->source_id, 30});
                }
            }
        }
        return GST_PAD_PROBE_OK;
    }

    static void on_sr_done_static(GstElement*, NvDsSRRecordingInfo* info, gpointer d) {
        auto* self = static_cast<AutoSmartRecordSystem*>(d);
        // Upload trong background
        self->upload_pool_.submit([self, path = std::string(info->filename)] {
            self->storage_->upload_async(path);
        });
    }

    gulong probe_id_ = 0;
    engine::core::utils::GstPadPtr probe_pad_;
    ThreadSafeQueue<RecordingRequest> recording_queue_;
    ThreadPool upload_pool_{2};
    std::shared_ptr<IStorageManager> storage_;
};
```

## 8. Probe trên Pad nào?

```
nvmultiurisrcbin.src  ──► [SRC PAD PROBE] ──► queue ──► nvinfer(pgie) ──►
                                                            │
                                           [SRC PAD PROBE] ┘
                                             (đọc detection results)
nvinfer.src ──► queue ──► nvtracker ──►
                               │
                [SRC PAD PROBE] ┘
                  (đọc tracked objects — có tracking ID)

nvtracker.src ──► nvdsanalytics ──►
                           │
             [SRC PAD PROBE] ┘
               (đọc analytics — ROI, line crossing)
```

**Quy tắc**: Probe trên **src pad** của element bạn muốn inspect output. Probe trên **sink pad** để intercept input trước khi element xử lý.
