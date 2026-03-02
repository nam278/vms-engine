# 09. Outputs — Encoders, Sinks & Smart Record

## 1. Tổng quan

Phase 4 (Outputs) và Phase 5 (Standalone) xử lý tất cả cấu hình đầu ra của pipeline:

| Output Type | GStreamer Element | Mô tả |
|------------|-----------------|-------|
| **Display** | `nveglglessink` / `nv3dsink` | Hiện thị trực tiếp lên màn hình |
| **File** | `filesink` | Ghi file MP4/MKV |
| **RTSP Streaming** | `rtspclientsink` | Stream qua RTSP |
| **Fake** | `fakesink` | Testing (drop frames) |
| **Smart Record** | `nvmultiurisrcbin.smart-record` | Event-triggered recording với pre-buffer |
| **App** | `appsink` | Custom processing trong code |

## 2. Encoder Support

### nvv4l2h264enc — H.264 (V4L2)

```cpp
// builders/encoder_builder.hpp
GstElement* EncoderBuilder::build_h264(
    const OutputConfig& cfg, const std::string& id, GstElement* pipeline)
{
    auto enc = make_gst_element("nvv4l2h264enc", id.c_str());
    if (!enc) return nullptr;

    g_object_set(G_OBJECT(enc.get()),
        "bitrate",             (guint)cfg.bitrate,         // bps
        "iframeinterval",      (guint)cfg.iframeinterval,
        "preset-level",        (guint)cfg.preset_level,    // 1=UltraFast, 4=Medium
        "insert-sps-pps",      (gboolean)cfg.insert_sps_pps,  // TRUE cho RTSP!
        "maxperf-enable",      (gboolean)cfg.maxperf_enable,
        "profile",             (guint)cfg.profile,         // 0=Baseline, 2=High
        nullptr);

    if (!gst_bin_add(GST_BIN(pipeline), enc.get())) return nullptr;
    return enc.release();
}
```

### nvv4l2h265enc — H.265 (HEVC)

```cpp
GstElement* EncoderBuilder::build_h265(
    const OutputConfig& cfg, const std::string& id, GstElement* pipeline)
{
    auto enc = make_gst_element("nvv4l2h265enc", id.c_str());
    if (!enc) return nullptr;

    g_object_set(G_OBJECT(enc.get()),
        "bitrate",           (guint)cfg.bitrate,
        "iframeinterval",    (guint)cfg.iframeinterval,
        "preset-level",      (guint)cfg.preset_level,
        "insert-vps-spspps", TRUE,  // H.265 tương đương insert-sps-pps
        nullptr);

    if (!gst_bin_add(GST_BIN(pipeline), enc.get())) return nullptr;
    return enc.release();
}
```

## 3. Sink Builders

### 3.1 Display Sink

```cpp
GstElement* SinkBuilder::build_display(
    const SinkConfig& cfg, const std::string& id, GstElement* pipeline)
{
    // Thử EGL sink trước (X11/Wayland)
    const char* sink_type = "nveglglessink";
    auto sink = make_gst_element(sink_type, id.c_str());

    if (!sink) {
        // Fallback cho headless/embedded
        LOG_W("nveglglessink not available, using fakesink for '{}'", id);
        sink = make_gst_element("fakesink", id.c_str());
    }

    if (sink) {
        g_object_set(G_OBJECT(sink.get()),
            "sync",    (gboolean)cfg.sync,
            nullptr);
    }

    if (!gst_bin_add(GST_BIN(pipeline), sink.get())) return nullptr;
    return sink.release();
}
```

### 3.2 File Sink

Pipeline: `[encoder] → mp4mux → filesink`

```cpp
GstElement* SinkBuilder::build_file(
    const SinkConfig& cfg, const std::string& id, GstElement* pipeline)
{
    // 1. Muxer
    auto mux = make_gst_element("mp4mux", (id + "_mux").c_str());
    g_object_set(G_OBJECT(mux.get()),
        "faststart",   TRUE,
        "fragment-duration", 2000,  // ms; tạo fragmented MP4
        nullptr);
    gst_bin_add(GST_BIN(pipeline), mux.get());

    // 2. File sink
    auto sink = make_gst_element("filesink", id.c_str());
    g_object_set(G_OBJECT(sink.get()),
        "location",   cfg.location.c_str(),
        "sync",       FALSE,
        "async",      FALSE,
        nullptr);

    if (!gst_bin_add(GST_BIN(pipeline), sink.get())) return nullptr;

    // Link mux → sink (mux là "visible" element cho linker)
    gst_element_link(mux.get(), sink.get());

    return mux.release();  // Trả về mux để linker connect encoder → mux
}
```

### 3.3 RTSP Sink

```cpp
GstElement* SinkBuilder::build_rtsp(
    const SinkConfig& cfg, const std::string& id, GstElement* pipeline)
{
    // Method 1: rtspclientsink (RTSP push — cần RTSP server nhận)
    auto sink = make_gst_element("rtspclientsink", id.c_str());
    if (sink) {
        g_object_set(G_OBJECT(sink.get()),
            "location",         cfg.rtsp_location.c_str(),
            "protocols",        GST_RTSP_LOWER_TRANS_TCP,
            "latency",          (guint)200,
            nullptr);
    }

    // Method 2: Nếu dùng GStreamer RTSP Server (tự host)
    // Xem docs/rtsp_server_setup.md

    if (!gst_bin_add(GST_BIN(pipeline), sink.get())) return nullptr;
    return sink.release();
}
```

## 4. Tee Element — Multiple Outputs từ 1 Stream

```
         ┌──► [q] ──► enc ──► rtspclientsink
OSD.src ──► tee ──┤
         └──► [q] ──► enc ──► filesink
```

```cpp
// outputs_builder.cpp
bool OutputsBuilder::build_with_tee(
    const OutputGroupConfig& group,
    GstElement* upstream_tail,
    GstElement* pipeline)
{
    // 1. Create tee
    auto tee = make_gst_element("tee", group.tee_id.c_str());
    gst_bin_add(GST_BIN(pipeline), tee.get());
    linker_->link(upstream_tail, tee.get());

    // 2. Mỗi sink là 1 branch
    for (const auto& sink_cfg : group.sinks) {
        // Queue trước mỗi branch (required với tee)
        auto q = make_queue(sink_cfg.id + "_q", defaults_);
        gst_bin_add(GST_BIN(pipeline), q.get());

        // Get request pad từ tee
        GstPad* tee_src = gst_element_request_pad_simple(tee.get(), "src_%u");
        GstPad* q_sink  = gst_element_get_static_pad(q.get(), "sink");
        gst_pad_link(tee_src, q_sink);
        gst_object_unref(tee_src);
        gst_object_unref(q_sink);

        // Build encoder + sink
        if (sink_cfg.needs_encoding()) {
            auto enc = encoder_builder_->build(config_, sink_cfg.id + "_enc", pipeline);
            linker_->link(q.get(), enc);

            GstElement* sink = build_sink(sink_cfg, pipeline);
            linker_->link(enc, sink);
        } else {
            GstElement* sink = build_sink(sink_cfg, pipeline);
            linker_->link(q.get(), sink);
        }
    }

    return true;
}
```

## 5. Smart Record — nvmultiurisrcbin Integration

Smart Record được **embedded trong `nvmultiurisrcbin`** — không phải element tách biệt trong pipeline chain. Được config và trigger qua GObject properties và signals.

### 5.1 Configuration

```yaml
sources:
  id: "src_muxer"
  smart_record: 1            # 0=off, 1=audio+video, 2=video-only
  smart_rec_dir_path: "dev/rec"
  smart_rec_file_prefix: "sr_"
  smart_rec_cache: 20        # Pre-event buffer (seconds)
  smart_rec_default_duration: 60  # Auto-stop sau N seconds
```

### 5.2 Trigger via NvDsSR API

```cpp
// pipeline/include/engine/pipeline/event_handlers/smart_record_handler.hpp
#include <gst-nvdssr.h>  // NvDsSR API (từ DeepStream SDK)

class SmartRecordHandler {
public:
    bool initialize(GstElement* src_element) {
        src_ = src_element;

        // Lấy NvDsSRContext từ nvmultiurisrcbin
        g_object_get(src_, "nvdssr-context", &sr_ctx_, nullptr);
        if (!sr_ctx_) {
            LOG_E("Could not get NvDsSRContext from source element");
            return false;
        }

        // Connect callback khi recording done
        // (thực ra dùng "sr-done" signal thay vì callback API trực tiếp)
        g_signal_connect(src_, "sr-done",
            G_CALLBACK(on_sr_done_static), this);

        return true;
    }

    /**
     * @brief Start smart recording cho một stream.
     * @param stream_id  Source ID (0-indexed, match với camera list)
     * @param duration   Duration in seconds; 0 = dùng default
     * @return session ID, dùng để stop sau
     */
    NvDsSRSessionId start_record(uint32_t stream_id, uint32_t duration = 0) {
        NvDsSRSessionId session_id = 0;

        NvDsSRStatus ret = NvDsSRStart(
            sr_ctx_,
            &session_id,
            stream_id,
            duration > 0 ? duration : config_.default_duration_sec,
            nullptr);

        if (ret != NVDSSR_STATUS_OK) {
            LOG_E("NvDsSRStart failed for stream {}: ret={}", stream_id, ret);
            return 0;
        }

        LOG_I("Smart record STARTED: stream={}, session={}, duration={}s",
              stream_id, session_id, duration);
        return session_id;
    }

    /**
     * @brief Stop recording cho một session.
     */
    bool stop_record(NvDsSRSessionId session_id) {
        NvDsSRStatus ret = NvDsSRStop(sr_ctx_, session_id);
        if (ret != NVDSSR_STATUS_OK) {
            LOG_E("NvDsSRStop failed: session={}, ret={}", session_id, ret);
            return false;
        }
        LOG_I("Smart record STOPPED: session={}", session_id);
        return true;
    }

private:
    static void on_sr_done_static(GstElement*, NvDsSRRecordingInfo* info,
                                   gpointer data) {
        auto* self = static_cast<SmartRecordHandler*>(data);
        self->on_sr_done(info);
    }

    void on_sr_done(NvDsSRRecordingInfo* info) {
        LOG_I("Smart record file ready: path={}, stream={}, duration={}s, size={}MB",
              info->filename, info->source_id,
              info->duration, info->file_size / 1024 / 1024);

        // Publish event
        event_publisher_->publish({
            .type = "smart_record_done",
            .stream_id = std::to_string(info->source_id),
            .file_path = info->filename,
            .duration_sec = info->duration
        });

        // Upload to cloud if configured
        if (config_.upload_after_record) {
            storage_manager_->upload_async(info->filename);
        }
    }

    NvDsSRContext*       sr_ctx_ = nullptr;
    GstElement*          src_    = nullptr;
    SmartRecordConfig    config_;
    IMessageProducer*    event_publisher_;
    IStorageManager*     storage_manager_;
};
```

### 5.3 Pre-event Buffer

Smart Record **tự động buffer** `smart_rec_cache` seconds trước khi trigger. Khi `start_record()` được gọi:

```
Timeline:
                ── t-20s ── t-15s ── t-10s ── t-5s ── [TRIGGER t=0] ── t+5s ── t+10s ──►
                ╔══════════════════════════════╗
Pre-buffer:     ║  Luôn có trong bộ nhớ        ║
                ╚══════════════════════════════╝
                                               ╔═══════════════════════════════╗
Recording:                                     ║  File được tạo từ đây         ║
                                               ╚═══════════════════════════════╝
```

Đây là lý do smart record rất hữu ích cho security — capture được sự kiện **trước** khi nó được phát hiện.

## 6. Output Pipeline Diagram

```
[Per-stream output group from demuxer]

demux.src_N
    │
    ▼
    queue
    │
    ▼
 tiler (optional — merge streams)
    │
    ▼
  nvdsosd
    │
    ▼
    tee ─────────────────────────────────────────────┐
    │                                                 │
    ▼                                                 ▼
  queue                                             queue
    │                                                 │
    ▼                                                 ▼
 nvv4l2h264enc                                   fakesink / display
    │
    ▼
  capsfilter
  (video/x-h264,stream-format=byte-stream,alignment=au)
    │
    ▼
  [mp4mux → filesink]
  OR
  [rtspclientsink]
```

## 7. YAML Config Example — Multiple Outputs

```yaml
outputs:
  - stream_id: "0"
    tee_id: "output_tee_0"     # Optional: auto-created nếu multi-sink
    sinks:

      # ── Display (development) ─────────────────────────────────────────
      - id: "display_0"
        type: "display"
        enabled: true
        sync: false

      # ── RTSP output ───────────────────────────────────────────────────
      - id: "rtsp_0"
        type: "rtsp"
        enabled: true
        rtsp_location: "rtsp://localhost:8554/stream0"
        codec: "h264"
        bitrate: 4000000        # 4 Mbps
        iframeinterval: 30
        preset_level: 1         # UltraFast
        insert_sps_pps: true    # Required cho RTSP!

      # ── File recording ────────────────────────────────────────────────
      - id: "file_0"
        type: "file"
        enabled: false
        location: "dev/rec/output_%05d.mp4"
        codec: "h264"
        bitrate: 6000000        # 6 Mbps

  - stream_id: "1"
    sinks:
      - id: "display_1"
        type: "display"
```
