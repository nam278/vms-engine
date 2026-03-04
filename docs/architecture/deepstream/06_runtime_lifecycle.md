# 06. Runtime Lifecycle — GstBus & Pipeline State Machine

## 1. Tổng quan

Sau khi pipeline được build, `PipelineManager` chịu trách nhiệm quản lý **toàn bộ vòng đời runtime**:

- Transition qua `PipelineState` machine
- Xử lý messages từ `GstBus` (EOS, Error, State changes, Custom messages)
- Graceful shutdown theo SIGINT/SIGTERM
- Error recovery (retry logic)

## 2. PipelineState Machine

```
    initialize()          start()           pause()
         │                  │                  │
         ▼                  ▼                  ▼
  Uninitialized ──────► Ready ──────────► Playing ◄────────► Paused
                                              │
                                        stop() │ hoặc
                                       error   │
                                              ▼
                                          Stopped / Error
```

```cpp
enum class PipelineState {
    Uninitialized,   // Trước initialize()
    Ready,           // Sau build thành công, chưa PLAYING
    Playing,         // GST_STATE_PLAYING đang chạy
    Paused,          // GST_STATE_PAUSED
    Stopped,         // gst_element_set_state(NULL) đã gọi
    Error            // Không recover được — log chi tiết
};
```

### State Transitions

```cpp
// PipelineManager implementation
bool PipelineManager::start() {
    if (state_ != PipelineState::Ready && state_ != PipelineState::Paused) {
        LOG_W("Cannot start from state: {}", to_string(state_.load()));
        return false;
    }

    GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        LOG_E("gst_element_set_state(PLAYING) failed");
        state_ = PipelineState::Error;
        return false;
    }

    state_ = PipelineState::Playing;
    LOG_I("Pipeline '{}' → PLAYING", config_name_);
    return true;
}

bool PipelineManager::pause() {
    if (state_ != PipelineState::Playing) return false;
    gst_element_set_state(pipeline_, GST_STATE_PAUSED);
    state_ = PipelineState::Paused;
    LOG_I("Pipeline '{}' → PAUSED", config_name_);
    return true;
}

bool PipelineManager::stop() {
    LOG_I("Stopping pipeline '{}'", config_name_);

    // 1. Loại bỏ bus watch trước
    if (bus_watch_id_ != 0) {
        g_source_remove(bus_watch_id_);
        bus_watch_id_ = 0;
    }

    // 2. Set NULL state để giải phóng resources
    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }

    state_ = PipelineState::Stopped;
    return true;
}
```

## 3. GstBus — Message Routing

`GstBus` là kênh giao tiếp giữa GStreamer pipeline thread và application. `PipelineManager` đăng ký một `watch` callback để nhận và xử lý messages.

```cpp
bool PipelineManager::initialize(PipelineConfig& config, GMainLoop* loop) {
    // ... build pipeline ...

    // Set up GstBus watch
    GstBus* bus = gst_element_get_bus(pipeline_);

    bus_watch_id_ = gst_bus_add_watch(
        bus,
        [](GstBus* bus, GstMessage* msg, gpointer data) -> gboolean {
            auto* self = static_cast<PipelineManager*>(data);
            return self->handle_bus_message(bus, msg);
        },
        this);

    gst_object_unref(bus);
    main_loop_ = loop;

    state_ = PipelineState::Ready;
    return true;
}
```

### `handle_bus_message()` — Message Handler

```cpp
gboolean PipelineManager::handle_bus_message(GstBus* bus, GstMessage* msg) {
    switch (GST_MESSAGE_TYPE(msg)) {

    // ── EOS (End of Stream) ────────────────────────────────────────────
    case GST_MESSAGE_EOS:
        LOG_I("EOS received on pipeline '{}'", config_name_);
        if (config_.sources.loop_on_eos) {
            // Seek về đầu (cho file sources)
            gst_element_seek_simple(pipeline_, GST_FORMAT_TIME,
                GST_SEEK_FLAG_FLUSH, 0);
        } else {
            stop();
            if (main_loop_) g_main_loop_quit(main_loop_);
        }
        break;

    // ── Error ─────────────────────────────────────────────────────────
    case GST_MESSAGE_ERROR: {
        GError* err = nullptr;
        gchar* debug_info = nullptr;
        gst_message_parse_error(msg, &err, &debug_info);

        {
            std::lock_guard<std::mutex> lock(error_mutex_);
            last_error_ = fmt::format("{}: {}", err->message, debug_info ? debug_info : "");
        }

        LOG_E("GStreamer ERROR from '{}': {}",
              GST_OBJECT_NAME(msg->src), last_error_);

        g_clear_error(&err);
        g_free(debug_info);

        state_ = PipelineState::Error;

        if (config_.pipeline.retry_on_error && retry_count_ < config_.pipeline.max_retries) {
            ++retry_count_;
            LOG_W("Attempting restart ({}/{})", retry_count_, config_.pipeline.max_retries);
            schedule_restart();
        } else {
            stop();
            if (main_loop_) g_main_loop_quit(main_loop_);
        }
        break;
    }

    // ── Warning ───────────────────────────────────────────────────────
    case GST_MESSAGE_WARNING: {
        GError* err = nullptr;
        gchar* debug_info = nullptr;
        gst_message_parse_warning(msg, &err, &debug_info);
        LOG_W("GStreamer WARNING from '{}': {} ({})",
              GST_OBJECT_NAME(msg->src), err->message, debug_info ? debug_info : "");
        g_clear_error(&err);
        g_free(debug_info);
        break;
    }

    // ── State Changed ─────────────────────────────────────────────────
    case GST_MESSAGE_STATE_CHANGED: {
        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(pipeline_)) {
            GstState old_state, new_state, pending;
            gst_message_parse_state_changed(msg, &old_state, &new_state, &pending);
            LOG_D("Pipeline state: {} → {} (pending: {})",
                gst_element_state_get_name(old_state),
                gst_element_state_get_name(new_state),
                gst_element_state_get_name(pending));

            if (new_state == GST_STATE_PLAYING) {
                // Export DOT graph khi pipeline đã PLAYING
                if (!config_.pipeline.dot_file_dir.empty()) {
                    GST_DEBUG_BIN_TO_DOT_FILE(
                        GST_BIN(pipeline_),
                        GST_DEBUG_GRAPH_SHOW_ALL,
                        config_.pipeline.id.c_str());
                    LOG_I("DOT graph exported to {}", config_.pipeline.dot_file_dir);
                }
            }
        }
        break;
    }

    // ── Custom Application Messages ───────────────────────────────────
    case GST_MESSAGE_APPLICATION: {
        const GstStructure* s = gst_message_get_structure(msg);
        const gchar* name = gst_structure_get_name(s);

        if (g_strcmp0(name, "SmartRecordStarted") == 0) {
            handle_smart_record_started(s);
        } else if (g_strcmp0(name, "SmartRecordStopped") == 0) {
            handle_smart_record_stopped(s);
        } else if (g_strcmp0(name, "StreamAdded") == 0) {
            handle_stream_added(s);
        } else if (g_strcmp0(name, "StreamRemoved") == 0) {
            handle_stream_removed(s);
        }
        break;
    }

    // ── Element Message (từ nvmultiurisrcbin, nvdssmartrecordbin) ──────
    case GST_MESSAGE_ELEMENT: {
        const GstStructure* s = gst_message_get_structure(msg);
        if (s) {
            const gchar* name = gst_structure_get_name(s);
            LOG_D("Element message: {} from {}", name, GST_OBJECT_NAME(msg->src));
        }
        break;
    }

    default:
        break;
    }

    return TRUE;  // Giữ watch (FALSE = remove watch)
}
```

## 4. Signal Handling — Graceful Shutdown

```cpp
// app/main.cpp
struct SignalContext {
    PipelineManager* manager;
    GMainLoop* loop;
};

static void setup_signal_handlers(PipelineManager* mgr, GMainLoop* loop) {
    static SignalContext ctx{mgr, loop};

    auto handler = [](int sig) {
        LOG_I("Received signal {} — initiating graceful shutdown", sig);
        ctx.manager->stop();
        if (ctx.loop) g_main_loop_quit(ctx.loop);
    };

    std::signal(SIGINT, handler);   // Ctrl+C
    std::signal(SIGTERM, handler);  // systemctl stop / docker stop
}
```

## 5. RTSP Source Reconnection

`nvmultiurisrcbin` hỗ trợ auto-reconnect:

```yaml
sources:
  rtsp_reconnect_interval: 10 # Retry sau 10 giây nếu mất kết nối
```

Khi một camera mất kết nối:

1. `nvmultiurisrcbin` phát `GstMessage` lên bus
2. `PipelineManager.handle_bus_message()` log warning
3. `nvmultiurisrcbin` tự reconnect sau interval

Không cần manual reconnect logic trong application code.

## 6. Dynamic Stream Add/Remove (CivetWeb REST API)

`nvmultiurisrcbin` tích hợp sẵn HTTP server (**CivetWeb** / `nvds_rest_server`) cho phép thêm/bỏ camera **lúc pipeline đang chạy** mà không cần restart.

> 📖 **Hướng dẫn đầy đủ**: xem [`10_rest_api.md`](10_rest_api.md)

### Cấu hình

```yaml
sources:
  rest_api_port: 9000 # 0=disable, >0=enable CivetWeb trên port đó
  drop_pipeline_eos: true # BẮt buộc khi dùng dynamic add/remove
  max_batch_size: 8 # Phải ≥ tổng số camera tối đa
```

### Add stream

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

### Remove stream

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

### Notes

- `value.camera_id`, `value.camera_url`, `value.change` là **mandatory**
- `change` phải chứa substring `"add"` hoặc `"remove"`
- `ip-address` property gây **SIGSEGV** trong DS8 — server luôn bind `0.0.0.0`
- Khi port mậu thuẫn: đổi `rest_api_port` hoặc dùng `rest_api_port: 0` để disable

## 7. Restart Logic

```cpp
void PipelineManager::schedule_restart() {
    // Sử dụng GLib timeout để restart sau 2 giây
    // (không trực tiếp từ bus callback — tránh deadlock)
    g_timeout_add_seconds(2, [](gpointer data) -> gboolean {
        auto* self = static_cast<PipelineManager*>(data);

        LOG_I("Restarting pipeline...");
        gst_element_set_state(self->pipeline_, GST_STATE_NULL);
        gst_element_set_state(self->pipeline_, GST_STATE_PLAYING);
        self->state_ = PipelineState::Playing;

        return G_SOURCE_REMOVE;  // Chỉ run một lần
    }, this);
}
```

## 8. PipelineInfo Query

```cpp
PipelineInfo PipelineManager::get_info() const {
    return PipelineInfo{
        .id       = config_id_,
        .name     = config_name_,
        .state    = state_.load(),
        .last_error = [&] {
            std::lock_guard<std::mutex> lock(error_mutex_);
            return last_error_;
        }(),
        .uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time_).count()
    };
}
```

## 9. Debugging Runtime Issues

```bash
# Monitor GStreamer state transitions
GST_DEBUG="GST_STATES:4" ./build/bin/vms_engine -c configs/default.yml

# Monitor bus messages
GST_DEBUG="GST_BUS:4" ./build/bin/vms_engine ...

# Full debug (rất verbose)
GST_DEBUG="5" ./build/bin/vms_engine ...

# Log DeepStream elements specifically
GST_DEBUG="nvinfer:5,nvtracker:4,nvmultiurisrcbin:3" ./build/bin/vms_engine ...

# Check pipeline state manually (GDB)
(gdb) p gst_element_get_state(pipeline_, nullptr, nullptr, 0)
```

### Common Runtime Error Messages

| GStreamer Message                 | Nguyên nhân                      | Fix                                    |
| --------------------------------- | -------------------------------- | -------------------------------------- |
| `Could not decode stream`         | Codec không được support         | Check CUDA/codec support, GPU driver   |
| `Internal data stream error`      | Buffer overflow hoặc decode fail | Giảm `batch_size`, tăng queue buffers  |
| `Failed to connect to RTSP`       | Camera offline                   | Kiểm tra `rtsp_reconnect_interval`     |
| `nvdsinfer: Failed to init model` | TensorRT engine fail             | Check TensorRT version, `.engine` file |
| `nvtracker: Failed to init`       | Tracker `.so` không tương thích  | Check `DEEPSTREAM_DIR` và tracker path |
