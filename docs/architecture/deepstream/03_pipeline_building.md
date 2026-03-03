# 03. Xây dựng Pipeline — 5 Phases

## 1. Tổng quan

Pipeline building được thực hiện bởi `PipelineBuilder` theo **5 phases tuần tự**. Mỗi phase được delegate cho một **block builder** chuyên biệt. `PipelineBuilder` đóng vai trò **orchestrator** — nó không tạo trực tiếp GstElement, mà điều phối qua factory + block builders.

**Pattern quan trọng**: `tails_` map — track **cuối cùng của mỗi upstream path** để biết link tới element tiếp theo ở đâu.

```
PipelineBuilder.build()
├── Phase 1: build_sources()      → SourceBuilder
├── Phase 2: build_processing()   → ProcessingBuilder
├── Phase 3: build_visuals()      → VisualsBuilder (optional)
├── Phase 4: build_outputs()      → OutputsBuilder
└── Phase 5: build_standalone()   → StandaloneBuilder
```

## 2. `tails_` Map Pattern

`tails_` là trái tim của linking system. Sau khi refactor sang **GstBin architecture**, mỗi phase tạo ra một sub-bin và lưu con trỏ bin đó vào `tails_`. Linking giữa các phase dùng `gst_element_link(bin_a, bin_b)` thông qua ghost pads.

```cpp
// Pipeline branches được track bởi string key:
std::unordered_map<std::string, GstElement*> tails_;

// Sau Phase 1 (sources_bin với ghost src pad):
tails_["src"] = sources_bin;  // GstBin pointer, KHÔNG phải nvmultiurisrcbin

// Sau Phase 2 (processing_bin với ghost sink + src pads):
tails_["src"] = processing_bin;  // cập nhật tail về bin mới nhất

// Sau Phase 3 (visuals_bin với ghost sink + src pads):
tails_["src"] = visuals_bin;

// Sau Phase 4 (output_bin_{id} per output, ghost sink):
// tails_ không cần cập nhật — outputs là đuôi pipeline, không có downstream
```

> **Tại sao GstBin?** Wrapping mỗi stage trong GstBin giúp:
>
> - Isolate internal linking (elements chỉ link với nhau trong cùng bin)
> - DOT graph rõ ràng hơn (group elements theo stage)
> - Ghost pads cung cấp consistent interface để link giữa phases

### Update `tails_` sau mỗi phase

```cpp
bool PipelineBuilder::build_sources(const PipelineConfig& config) {
    auto builder = std::make_unique<SourceBlockBuilder>(pipeline_, tails_);
    if (!builder->build(config)) return false;
    // tails_["src"] = sources_bin  (set inside SourceBlockBuilder)
    return true;
}

bool PipelineBuilder::build_processing(const PipelineConfig& config) {
    auto builder = std::make_unique<ProcessingBlockBuilder>(pipeline_, tails_);
    if (!builder->build(config)) return false;
    // ProcessingBlockBuilder links sources_bin → processing_bin via ghost pads
    // tails_["src"] updated to processing_bin
    return true;
}
```

## 3. Phase 1 — Sources

**Block builder**: `SourceBlockBuilder` (trong `pipeline/src/block_builders/`)

Phase 1 tạo `sources_bin` — một `GstBin` chứa `nvmultiurisrcbin`. Sau đó expose ghost src pad để downstream bins có thể link.

```cpp
bool SourceBlockBuilder::build(const PipelineConfig& config) {
    // 1. Tạo sources_bin
    GstElement* sources_bin = gst_bin_new("sources_bin");

    // 2. Build nvmultiurisrcbin bên trong sources_bin
    builders::SourceBuilder src_builder(sources_bin);
    GstElement* source = src_builder.build(config, 0);

    // 3. Expose ghost src pad từ nvmultiurisrcbin
    GstPad* src_pad = gst_element_get_static_pad(source, "src");
    GstPad* ghost_src = gst_ghost_pad_new("src", src_pad);
    gst_element_add_pad(sources_bin, ghost_src);

    // 4. Add sources_bin vào top-level pipeline
    gst_bin_add(GST_BIN(pipeline_), sources_bin);

    tails_["src"] = sources_bin;  // lưu bin, KHÔNG phải element
    return true;
}
```

**SourceBuilder** (element builder) cấu hình `nvmultiurisrcbin`:

```cpp
GstElement* SourceBuilder::build(const PipelineConfig& config, int /*index*/) {
    const auto& src = config.sources;

    auto elem = make_gst_element("nvmultiurisrcbin", "nvmultiurisrcbin0");

    // Group 1 — direct (ip-address/port intentionally OMITTED — DS8 SIGSEGV)
    g_object_set(G_OBJECT(elem.get()),
        "max-batch-size", (gint) src.max_batch_size,
        "mode",           (gint) src.mode,  // 0=video, 1=audio
        nullptr);

    // Group 2 — nvurisrcbin per-source passthrough
    g_object_set(G_OBJECT(elem.get()),
        "gpu-id",                  (gint) src.gpu_id,
        "select-rtp-protocol",     (gint) src.select_rtp_protocol,
        "rtsp-reconnect-interval", (gint) src.rtsp_reconnect_interval,
        "drop-pipeline-eos",       (gboolean) src.drop_pipeline_eos,
        nullptr);

    // Group 3 — nvstreammux passthrough
    g_object_set(G_OBJECT(elem.get()),
        "width",  (gint) src.width,
        "height", (gint) src.height,
        "live-source", (gboolean) src.live_source,
        nullptr);

    // uri-list (comma-separated)
    g_object_set(G_OBJECT(elem.get()), "uri-list", uri_list.c_str(), nullptr);

    gst_bin_add(GST_BIN(bin_), elem.get());
    return elem.release();
}
```

> ⚠️ **DS8 Bug**: `ip-address` và `port` KHÔNG được set — `g_object_set("ip-address", ...)` gây SIGSEGV trong DeepStream 8.0 bất kể timing. Element sử dụng giá trị mặc định: `0.0.0.0`, REST API disabled.

## 4. Phase 2 — Processing

**Block builder**: `ProcessingBuilder`

Xử lý theo thứ tự elements trong `config.processing`:

```yaml
# Ví dụ YAML — thứ tự = thứ tự build
processing:
  elements:
    - id: "pgie"
      role: "primary_inference"
      queue: {} # Tự động insert GstQueue trước element này
      type: "nvinfer"
      config_file_path: "configs/nvinfer/pgie_config.txt"
      unique_id: 1
      process_mode: 1
      batch_size: 4

    - id: "tracker"
      role: "tracker"
      queue: {}
      ll_lib_file: "/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so"
      ll_config_file: "configs/tracker/nvdcf_config.yml"

    - id: "sgie_lpr"
      role: "secondary_inference"
      queue: {}
      type: "nvinfer"
      config_file_path: "configs/nvinfer/sgie_lpr_config.txt"
      unique_id: 2
      process_mode: 2
      operate_on_gie_id: 1
      operate_on_class_ids: "2" # class 2 = vehicles

    - id: "demuxer"
      role: "demuxer"
      queue: {}
```

```cpp
bool ProcessingBuilder::build_phase(const PipelineConfig& config, ...) {
    GstElement* tail = tails["source"];

    for (const auto& elem : config.processing) {
        // ─── Queue insertion ─────────────────────────────────────
        if (elem.has_queue_config()) {
            auto* q = build_queue_for(elem, config.queue_defaults, pipeline);
            link_manager_->link_elements(tail, q);
            tail = q;
        }

        // ─── Determine builder type ──────────────────────────────
        std::unique_ptr<IElementBuilder> builder;
        if (elem.role == "primary_inference" || elem.role == "secondary_inference")
            builder = factory_->create_processing_builder("inference");
        else if (elem.role == "tracker")
            builder = factory_->create_processing_builder("tracker");
        else if (elem.role == "demuxer")
            builder = factory_->create_processing_builder("demuxer");
        // ...

        auto* gst_elem = builder->build(config, elem.id, pipeline);
        if (!gst_elem) return false;

        link_manager_->link_elements(tail, gst_elem);
        tail = gst_elem;
    }

    tails["processing_tail"] = tail;
    return true;
}
```

### Special Case: `nvstreamdemux`

`nvstreamdemux` tạo dynamic source pads (một per stream). Sau khi add demux, phải dùng **pad-added signal** để link:

```cpp
// Sau khi demuxer được add và linked:
g_signal_connect(demux, "pad-added",
    G_CALLBACK([](GstElement* src, GstPad* pad, gpointer data) {
        auto* pipeline = static_cast<GstElement*>(data);
        // Xác định stream ID từ pad name "src_0", "src_1"...
        // Lưu pad vào tails_["stream_0"] = pad_owner_element
    }), pipeline);
```

> Xem chi tiết về dynamic pads → [04_linking_system.md](04_linking_system.md)

## 5. Phase 3 — Visuals

**Block builder**: `VisualsBuilder`

Tùy chọn — skip nếu `config.visuals.enabled = false`.

```cpp
bool VisualsBuilder::build_phase(const PipelineConfig& config, ...) {
    if (!config.visuals.enabled) {
        // Copy tails từ stream outputs của demuxer
        return true;
    }

    for (const auto& stream_id : get_stream_ids(config)) {
        GstElement* tail = tails["stream_" + stream_id];

        // ─── Tiler ─────────────────────────────────────────────
        if (config.visuals.tiler.enabled) {
            auto* tiler = factory_->create_visual_builder("tiler")
                ->build(config, "tiler", pipeline);
            link_manager_->link(tail, tiler);
            tail = tiler;
        }

        // ─── OSD ───────────────────────────────────────────────
        if (config.visuals.osd.enabled) {
            auto queue_name = "osd_queue_" + stream_id;
            auto* q = build_queue(queue_name, config.queue_defaults, pipeline);
            link_manager_->link(tail, q);

            auto* osd = factory_->create_visual_builder("osd")
                ->build(config, "osd_" + stream_id, pipeline);
            link_manager_->link(q, osd);
            tail = osd;
        }

        tails["vis_" + stream_id] = tail;
    }
    return true;
}
```

## 6. Phase 4 — Outputs

**Block builder**: `OutputsBuilder`

Tạo tee → nhiều sinks cho mỗi stream:

```cpp
bool OutputsBuilder::build_phase(const PipelineConfig& config, ...) {
    for (const auto& output : config.outputs) {
        GstElement* tail = tails["vis_" + output.stream_id];

        // ─── Tee (nếu multiple outputs cho cùng stream) ─────────
        if (output.requires_tee) {
            auto* tee = gst_element_factory_make("tee", output.stream_id.c_str());
            gst_bin_add(GST_BIN(pipeline), tee);
            link_manager_->link(tail, tee);
            tail = tee;
        }

        for (const auto& sink_cfg : output.sinks) {
            // Queue trước encoder
            auto* q = build_queue("enc_q_" + sink_cfg.id, config.queue_defaults, pipeline);

            // Encoder (nếu cần)
            GstElement* encode_tail = q;
            if (sink_cfg.needs_encoding()) {
                auto* enc = factory_->create_encoder_builder(sink_cfg.codec)
                    ->build(config, sink_cfg.id + "_enc", pipeline);
                link_manager_->link(q, enc);
                encode_tail = enc;
            }

            // Sink
            auto* sink = factory_->create_sink_builder(sink_cfg.type)
                ->build(config, sink_cfg.id, pipeline);
            link_manager_->link(encode_tail, sink);
        }

        tails["output_" + output.stream_id] = tail;
    }
    return true;
}
```

## 7. Phase 5 — Standalone

**Block builder**: `StandaloneBuilder`

Các elements không có trong main pipeline chain — thêm vào sau khi linking hoàn tất:

```cpp
bool StandaloneBuilder::build_phase(const PipelineConfig& config, ...) {
    // ─── Smart Record ─────────────────────────────────────────
    if (config.smart_record.has_value()) {
        const auto& sr = config.smart_record.value();
        // nvdssmartrecordbin đã được embedded trong nvmultiurisrcbin
        // → chỉ cần configure signals:
        auto* src_elem = tails["source"];
        g_object_set(G_OBJECT(src_elem),
            "smart-record",                  sr.mode,
            "smart-rec-dir-path",            sr.output_dir.c_str(),
            "smart-rec-file-prefix",         sr.file_prefix.c_str(),
            "smart-rec-cache",               sr.post_event_duration_sec,
            "smart-rec-default-duration",    sr.default_duration_sec,
            nullptr);
    }

    // ─── Message Broker ───────────────────────────────────────
    if (config.message_broker.has_value()) {
        auto* msgconv = factory_->create_msgbroker_builder()
            ->build(config, "msgconv", pipeline);
        // nvmsgconv + nvmsgbroker được linked từ probe handler
        // (không phải từ main chain)
    }

    return true;
}
```

## 8. DOT Graph Export

Sau khi build hoàn tất (ở Phase 5), nếu `config.pipeline.dot_file_dir` được set:

```cpp
// Cuối build():
if (!config.pipeline.dot_file_dir.empty()) {
    // Set env var cho GStreamer
    setenv("GST_DEBUG_DUMP_DOT_DIR",
           config.pipeline.dot_file_dir.c_str(), 1);

    // Trigger khi pipeline chuyển sang READY state:
    // GStreamer tự export file: de<n>_<state>.dot
    GST_DEBUG_BIN_TO_DOT_FILE(
        GST_BIN(pipeline_),
        GST_DEBUG_GRAPH_SHOW_ALL,
        "vms_engine_pipeline");
}
```

```bash
# Convert DOT → PNG
dot -Tpng dev/logs/vms_engine_pipeline.dot -o pipeline.png
xdg-open pipeline.png
```

## 9. Error Handling within Build

Mỗi phase trả về `bool`. Nếu một phase fail, `build()` dừng ngay:

```cpp
bool PipelineBuilder::build(const PipelineConfig& config, GMainLoop* loop) {
    // 1. Tạo GstPipeline
    pipeline_ = gst_pipeline_new(config.pipeline.id.c_str());
    if (!pipeline_) { LOG_E("gst_pipeline_new failed"); return false; }

    // 2. Run phases — dừng ngay nếu bất kỳ phase nào fail
    if (!build_sources(config))     { cleanup(); return false; }
    if (!build_processing(config))  { cleanup(); return false; }
    if (!build_visuals(config))     { cleanup(); return false; }
    if (!build_outputs(config))     { cleanup(); return false; }
    if (!build_standalone(config))  { cleanup(); return false; }

    LOG_I("Pipeline '{}' successfully built", config.pipeline.name);
    return true;
}

void PipelineBuilder::cleanup() {
    if (pipeline_) {
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }
    tails_.clear();
}
```
