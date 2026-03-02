# 07. Event Handlers & Pad Probes

## 1. Tổng quan

vms-engine có hai cơ chế để can thiệp vào GStreamer pipeline:

| Cơ chế | Khi nào | API |
|--------|---------|-----|
| **Signal-based (Event Handlers)** | Khi cần react theo GStreamer element events (new-sample, buffer, notify) | `g_signal_connect()` |
| **Pad Probe** | Khi cần inspect/modify metadata ở buffer level trên pad | `gst_pad_add_probe()` |

Hai hệ thống này được quản lý độc lập bởi `HandlerManager` và `ProbeHandlerManager`.

> Xem so sánh chi tiết: [10_signal_vs_probe_deep_dive.md](10_signal_vs_probe_deep_dive.md)

## 2. HandlerManager — Signal-based Handlers

### Role

`HandlerManager` quản lý lifecycle của tất cả `IEventHandler` implementations:
- Load handler implementations (từ plugin `.so` hoặc built-in)
- Connect signals lên target elements
- Disconnect khi pipeline dừng

```cpp
// pipeline/include/engine/pipeline/event_handlers/handler_manager.hpp
class HandlerManager {
public:
    explicit HandlerManager(GstElement* pipeline);

    /// Đăng ký + connect một handler lên element
    bool register_and_connect(
        std::unique_ptr<engine::core::eventing::IEventHandler> handler,
        const engine::core::config::CustomHandlerConfig& config);

    /// Disconnect tất cả handlers (gọi khi stop pipeline)
    void disconnect_all();

    /// Lấy handler theo ID
    IEventHandler* get_handler(const std::string& handler_id);

private:
    GstElement* pipeline_;
    std::vector<std::unique_ptr<IEventHandler>> handlers_;
    std::unordered_map<std::string, IEventHandler*> handler_map_;
};
```

### IEventHandler Interface

```cpp
// core/include/engine/core/eventing/ievent_handler.hpp
class IEventHandler {
public:
    virtual ~IEventHandler() = default;

    virtual bool connect(
        GstElement* element,
        const engine::core::config::CustomHandlerConfig& config) = 0;

    virtual void disconnect() = 0;
    virtual std::string name() const = 0;
};
```

## 3. Built-in Event Handlers

### 3.1 CropDetectedObjHandler

Lấy frames từ `appsink`, crop các detected objects và lưu ảnh JPEG.

```cpp
// pipeline/include/engine/pipeline/event_handlers/crop_detected_obj_handler.hpp
class CropDetectedObjHandler : public IEventHandler {
public:
    bool connect(GstElement* appsink, const CustomHandlerConfig& cfg) override {
        config_ = cfg;
        storage_ = resolve_storage(cfg);

        // Connect new-sample signal
        signal_id_ = g_signal_connect(
            G_OBJECT(appsink), "new-sample",
            G_CALLBACK(on_new_sample_static), this);

        return signal_id_ != 0;
    }

    void disconnect() override {
        if (appsink_ && signal_id_ != 0) {
            g_signal_handler_disconnect(G_OBJECT(appsink_), signal_id_);
            signal_id_ = 0;
        }
    }

private:
    static GstFlowReturn on_new_sample_static(GstElement* sink, gpointer data) {
        return static_cast<CropDetectedObjHandler*>(data)->on_new_sample(sink);
    }

    GstFlowReturn on_new_sample(GstElement* sink) {
        // 1. Pull sample
        GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
        if (!sample) return GST_FLOW_EOS;

        GstBuffer* buf = gst_sample_get_buffer(sample);
        if (!buf) {
            gst_sample_unref(sample);
            return GST_FLOW_OK;
        }

        // 2. Get NvDs metadata
        NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buf);
        if (!batch_meta) {
            gst_sample_unref(sample);
            return GST_FLOW_OK;
        }

        // 3. Iterate frames
        for (NvDsFrameMetaList* fl = batch_meta->frame_meta_list; fl; fl = fl->next) {
            auto* frame_meta = static_cast<NvDsFrameMeta*>(fl->data);

            // 4. Iterate objects
            for (NvDsObjectMetaList* ol = frame_meta->obj_meta_list; ol; ol = ol->next) {
                auto* obj_meta = static_cast<NvDsObjectMeta*>(ol->data);

                if (obj_meta->confidence < config_.min_confidence) continue;
                if (!should_crop_class(obj_meta->class_id, config_.class_ids)) continue;

                // 5. Crop và save
                crop_and_save(buf, frame_meta, obj_meta);
            }
        }

        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    void crop_and_save(GstBuffer* buf, NvDsFrameMeta* frame_meta,
                       NvDsObjectMeta* obj_meta);

    GstElement* appsink_ = nullptr;
    gulong signal_id_ = 0;
    CustomHandlerConfig config_;
    std::shared_ptr<IStorageManager> storage_;
};
```

### 3.2 SmartRecordHandler

Quản lý nvdssmartrecordbin bắt đầu/dừng recording.

```cpp
class SmartRecordHandler : public IEventHandler {
public:
    bool connect(GstElement* src, const CustomHandlerConfig& cfg) override {
        // Connect signal "sr-done" → nvmultiurisrcbin
        g_signal_connect(G_OBJECT(src), "sr-done",
            G_CALLBACK(on_sr_done_static), this);
        return true;
    }

    /// Trigger smart record start (gọi từ REST API hoặc probe)
    bool start_record(int stream_id, int duration_sec) {
        NvDsSRSessionId session_id;
        NvDsSRStart(nvsr_ctx_, &session_id, stream_id, duration_sec, nullptr);
        return true;
    }

    bool stop_record(NvDsSRSessionId session_id) {
        NvDsSRStop(nvsr_ctx_, session_id);
        return true;
    }

private:
    static void on_sr_done_static(GstElement*, NvDsSRRecordingInfo* info, gpointer d) {
        auto* self = static_cast<SmartRecordHandler*>(d);
        LOG_I("Smart record done: file={}, duration={}s",
              info->filename, info->duration);
        // Upload to storage nếu config.upload = true
        if (self->config_.upload_after_record) {
            self->storage_->upload(info->filename);
        }
    }
};
```

### 3.3 ExtProcHandler

Gửi frames tới external HTTP processor (POST multipart/form-data).

```cpp
class ExtProcHandler : public IEventHandler {
public:
    bool connect(GstElement* appsink, const CustomHandlerConfig& cfg) override {
        http_client_ = std::make_shared<engine::services::TritonHttpClient>(
            cfg.get_config("base_url"), cfg.get_config_int("timeout_ms", 500));

        g_signal_connect(G_OBJECT(appsink), "new-sample",
            G_CALLBACK(on_new_sample_static), this);
        return true;
    }

private:
    GstFlowReturn on_new_sample(GstElement* sink) {
        auto sample = pull_sample_safe(sink);
        if (!sample) return GST_FLOW_EOS;

        // Async HTTP POST (không block pipeline thread)
        auto frame_data = extract_frame_jpeg(sample.get());
        if (frame_data) {
            thread_pool_.submit([this, data = std::move(*frame_data)] {
                http_client_->post("/process", data);
            });
        }
        return GST_FLOW_OK;
    }

    std::shared_ptr<IExternalInferenceClient> http_client_;
    ThreadPool thread_pool_{4};
};
```

## 4. ProbeHandlerManager — Pad Probes

### Role

`ProbeHandlerManager` attach GStreamer pad probes lên các elements đã được build. Probes được chạy trực tiếp trên **pipeline streaming thread** — phải nhanh, không block.

```cpp
// pipeline/include/engine/pipeline/probes/probe_handler_manager.hpp
class ProbeHandlerManager {
public:
    explicit ProbeHandlerManager(GstElement* pipeline);

    /// Attach probe handler lên pad của element ID
    bool attach(std::unique_ptr<IProbeHandler> handler,
                const std::string& element_id,
                const std::string& pad_name,
                GstPadProbeType probe_type = GST_PAD_PROBE_TYPE_BUFFER);

    /// Remove tất cả probes
    void detach_all();

private:
    GstElement* pipeline_;
    std::vector<std::unique_ptr<IProbeHandler>> handlers_;
};
```

## 5. Built-in Probe Handlers

### 5.1 ClassIdNamespaceHandler

Resolves class ID conflicts khi có nhiều SGIE với overlapping class IDs.

```cpp
// pipeline/include/engine/pipeline/probes/class_id_namespace_handler.hpp
class ClassIdNamespaceHandler : public IProbeHandler {
public:
    bool attach(GstElement* nvinfer_sgie, const std::string& pad_name,
                GstPadProbeType type) override {
        GstPad* pad = gst_element_get_static_pad(nvinfer_sgie, pad_name.c_str());
        if (!pad) return false;

        probe_id_ = gst_pad_add_probe(
            pad, type,
            [](GstPad*, GstPadProbeInfo* info, gpointer d) {
                return static_cast<ClassIdNamespaceHandler*>(d)->on_buffer(
                    nullptr, info);
            }, this, nullptr);

        gst_object_unref(pad);
        return probe_id_ != 0;
    }

    GstPadProbeReturn on_buffer(GstPad*, GstPadProbeInfo* info) override {
        GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
        NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buf);
        if (!batch_meta) return GST_PAD_PROBE_RETURN(GST_PAD_PROBE_OK);

        for (auto* fl = batch_meta->frame_meta_list; fl; fl = fl->next) {
            auto* frame = static_cast<NvDsFrameMeta*>(fl->data);
            for (auto* ol = frame->obj_meta_list; ol; ol = ol->next) {
                auto* obj = static_cast<NvDsObjectMeta*>(ol->data);
                // Offset class_id để tránh conflict với primary gie
                if (obj->unique_component_id == sgie_unique_id_) {
                    obj->class_id += class_id_offset_;
                }
            }
        }
        return GST_PAD_PROBE_OK;
    }

private:
    gulong probe_id_ = 0;
    int sgie_unique_id_;
    int class_id_offset_;
};
```

### 5.2 SmartRecordProbeHandler

Tự động trigger smart record khi phát hiện object trong ROI.

```cpp
class SmartRecordProbeHandler : public IProbeHandler {
public:
    GstPadProbeReturn on_buffer(GstPad*, GstPadProbeInfo* info) override {
        auto* buf = GST_PAD_PROBE_INFO_BUFFER(info);
        auto* meta = gst_buffer_get_nvds_batch_meta(buf);
        if (!meta) return GST_PAD_PROBE_OK;

        for (auto* fl = meta->frame_meta_list; fl; fl = fl->next) {
            auto* frame = static_cast<NvDsFrameMeta*>(fl->data);

            for (auto* ol = frame->obj_meta_list; ol; ol = ol->next) {
                auto* obj = static_cast<NvDsObjectMeta*>(ol->data);

                if (should_trigger(obj)) {
                    LOG_D("Auto-triggering smart record for stream {}",
                          frame->source_id);
                    sr_handler_->start_record(frame->source_id,
                                              config_.default_duration_sec);
                }
            }
        }
        return GST_PAD_PROBE_OK;
    }

private:
    bool should_trigger(NvDsObjectMeta* obj) {
        return obj->confidence >= config_.min_confidence &&
               std::ranges::contains(config_.class_ids, obj->class_id);
    }

    SmartRecordHandler* sr_handler_;
    SmartRecordAutoTriggerConfig config_;
};
```

### 5.3 CropObjectProbeHandler

Crop detected objects và upload lên storage — phiên bản probe (nhanh hơn, không cần `appsink`).

```cpp
class CropObjectProbeHandler : public IProbeHandler {
public:
    GstPadProbeReturn on_buffer(GstPad* pad, GstPadProbeInfo* info) override {
        auto* buf = GST_PAD_PROBE_INFO_BUFFER(info);
        NvDsBatchMeta* meta = gst_buffer_get_nvds_batch_meta(buf);

        // Map buffer để truy cập CUDA memory
        NvBufSurface* surface = nullptr;
        if (NvBufSurfaceMap(get_surface_from_buffer(buf), &surface) != 0) {
            return GST_PAD_PROBE_OK;
        }

        // Process objects
        for (auto* fl = meta->frame_meta_list; fl; fl = fl->next) {
            auto* frame = static_cast<NvDsFrameMeta*>(fl->data);
            for (auto* ol = frame->obj_meta_list; ol; ol = ol->next) {
                auto* obj = static_cast<NvDsObjectMeta*>(ol->data);
                if (obj->confidence >= config_.min_confidence) {
                    async_save_crop(surface, frame, obj);
                }
            }
        }

        NvBufSurfaceUnMap(surface);
        return GST_PAD_PROBE_OK;  // Không modify, cho buffer đi qua
    }
};
```

## 6. Attach Probe vs Connect Handler (Quyết định)

```
Cần modify/drop buffer?
    YES → Pad Probe
    NO  ┐
        ├── Cần frame raw data (crop)?
        │   YES → Signal (appsink new-sample) OR Probe (trong src pad)
        │   Rec: Probe nếu cần ít overhead, Signal nếu cần full sample context
        │
        ├── Cần react theo file path/time (smart record done)?
        │   YES → Signal (sr-done từ nvmultiurisrcbin)
        │
        └── Cần inspect NvDs metadata?
            YES → Pad Probe (src pad của nvinfer hoặc nvtracker)
```

## 7. Custom Handler Loading từ Config

```yaml
# config.yml
custom_handlers:
  - id: "my_crop_handler"
    type: "crop_detected_obj"
    target_element: "osd_0"
    config:
      output_dir: "dev/rec/objects"
      min_confidence: 0.75
      class_ids: [0, 2, 3]
      upload_to: "local_storage"
```

```cpp
// PipelineManager::register_event_handlers()
bool PipelineManager::register_event_handlers(
    std::vector<CustomHandlerConfig>& handlers_config)
{
    for (auto& h_cfg : handlers_config) {
        // Resolve target element
        GstElement* target = gst_bin_get_by_name(
            GST_BIN(pipeline_), h_cfg.target_element.c_str());

        if (!target) {
            LOG_W("Handler '{}': target element '{}' not found",
                  h_cfg.id, h_cfg.target_element);
            continue;
        }

        // Create handler từ type string
        auto handler = handler_registry_.create(h_cfg.type);
        if (!handler) {
            LOG_E("Unknown handler type: {}", h_cfg.type);
            gst_object_unref(target);
            continue;
        }

        handler_manager_->register_and_connect(std::move(handler), h_cfg);
        gst_object_unref(target);
    }
    return true;
}
```
