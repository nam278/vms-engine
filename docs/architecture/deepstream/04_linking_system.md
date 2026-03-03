# 04. Linking System — Kết nối GStreamer Elements

## 1. Tổng quan

`PipelineLinker` (hay `LinkManager`) là component chuyên trách **kết nối** (link) các GstElement với nhau sau khi chúng được tạo và add vào pipeline. Đây là tầng trừu tượng trên `gst_element_link` và `gst_pad_link` của GStreamer.

Có hai loại linking:

| Loại                    | API GStreamer                         | Khi nào dùng                                                   |
| ----------------------- | ------------------------------------- | -------------------------------------------------------------- |
| **Static pad linking**  | `gst_element_link()`                  | Elements có fixed src/sink pads (nvinfer, nvtracker, osd, ...) |
| **Dynamic pad linking** | `gst_pad_link()` + `pad-added` signal | nvstreamdemux (tạo src pads khi stream join)                   |

## 2. PipelineLinker Interface và Implementation

```cpp
// pipeline/include/engine/pipeline/linking/pipeline_linker.hpp
namespace engine::pipeline {

class PipelineLinker {
public:
    explicit PipelineLinker(GstElement* pipeline);

    /**
     * @brief Link src → sink bằng static pad.
     * Wrapper đơn giản cho gst_element_link với LOG_E khi fail.
     */
    bool link(GstElement* src, GstElement* sink);

    /**
     * @brief Link src → sink qua tên pad cụ thể.
     * Dùng khi cần link vào named pad (tee src_%u, nvstreamdemux src_%u).
     */
    bool link_pads(GstElement* src, const std::string& src_pad,
                   GstElement* sink, const std::string& sink_pad);

    /**
     * @brief Kết nối signal pad-added cho dynamic sources.
     * Callback được gọi khi nvstreamdemux tạo pad mới.
     */
    void connect_pad_added(GstElement* element,
                           std::function<void(GstPad*)> callback);

    /**
     * @brief Link nhiều elements theo thứ tự trong vector.
     * Convenience: link_chain({src, q, nvinfer, nvtracker, ...})
     */
    bool link_chain(const std::vector<GstElement*>& elements);

    /**
     * @brief Insert queue trước sink, link src → queue → sink.
     */
    bool link_with_queue(GstElement* src, GstElement* sink,
                         GstElement* queue);

private:
    GstElement* pipeline_;
};

} // namespace engine::pipeline
```

## 3. Static Pad Linking

Đa số elements trong DeepStream pipeline có **static sink/src pads** và có thể link trực tiếp:

```
nvmultiurisrcbin.src → [queue] → nvinfer.sink
nvinfer.src → [queue] → nvtracker.sink
nvtracker.src → [queue] → nvinfer_sgie.sink
...
```

```cpp
bool PipelineLinker::link(GstElement* src, GstElement* sink) {
    if (!src || !sink) {
        LOG_E("link: null element (src={}, sink={})",
              src ? GST_ELEMENT_NAME(src) : "NULL",
              sink ? GST_ELEMENT_NAME(sink) : "NULL");
        return false;
    }

    if (!gst_element_link(src, sink)) {
        LOG_E("gst_element_link failed: {} → {}",
              GST_ELEMENT_NAME(src),
              GST_ELEMENT_NAME(sink));
        return false;
    }

    LOG_D("Linked: {} → {}", GST_ELEMENT_NAME(src), GST_ELEMENT_NAME(sink));
    return true;
}

bool PipelineLinker::link_chain(const std::vector<GstElement*>& elements) {
    for (size_t i = 0; i + 1 < elements.size(); ++i) {
        if (!link(elements[i], elements[i + 1])) return false;
    }
    return true;
}
```

## 4. Dynamic Pad Linking — nvstreamdemux

`nvstreamdemux` là trường hợp đặc biệt: nó không tạo src pads ngay lập tức mà tạo chúng khi stream được demuxed (dynamic pads).

### Cách hoạt động

```
nvtracker.src → nvstreamdemux.sink
                  │
                  ├── nvstreamdemux.src_0 [created dynamically]
                  ├── nvstreamdemux.src_1 [created dynamically]
                  └── nvstreamdemux.src_N [created dynamically]
```

### Implementation

```cpp
void PipelineLinker::connect_pad_added(
    GstElement* demux,
    std::function<void(GstPad*)> callback)
{
    struct CallbackData {
        std::function<void(GstPad*)> fn;
    };

    auto* data = new CallbackData{std::move(callback)};

    g_signal_connect_data(
        G_OBJECT(demux),
        "pad-added",
        G_CALLBACK([](GstElement*, GstPad* new_pad, gpointer user_data) {
            auto* d = static_cast<CallbackData*>(user_data);
            d->fn(new_pad);
        }),
        data,
        nullptr,   // GClosureNotify
        static_cast<GConnectFlags>(0));
}
```

### Pattern sử dụng trong ProcessingBuilder/OutputsBuilder

```cpp
// ProcessingBuilder: sau khi add demux
g_signal_connect(
    demux, "pad-added",
    G_CALLBACK(on_demux_pad_added),
    &context);

// Callback
static void on_demux_pad_added(GstElement* demux, GstPad* new_pad,
                                gpointer user_data) {
    auto* ctx = static_cast<BuildContext*>(user_data);

    // Parse stream ID từ pad name "src_0", "src_1"
    std::string pad_name = GST_PAD_NAME(new_pad);
    int stream_idx = std::stoi(pad_name.substr(4)); // "src_0" → 0

    // Lấy next element cho stream này
    auto it = ctx->stream_next.find(stream_idx);
    if (it == ctx->stream_next.end()) {
        LOG_W("No downstream element for stream {}", stream_idx);
        return;
    }
    GstElement* next = it->second;

    // Get sink pad của next element
    GstPad* sink_pad = gst_element_get_static_pad(next, "sink");
    if (!sink_pad) {
        sink_pad = gst_element_request_pad_simple(next, "sink_%u");
    }

    if (!sink_pad) {
        LOG_E("Cannot get sink pad for stream_{}", stream_idx);
        return;
    }

    GstPadLinkReturn ret = gst_pad_link(new_pad, sink_pad);
    if (ret != GST_PAD_LINK_OK) {
        LOG_E("gst_pad_link failed for stream_{}: {}", stream_idx, ret);
    } else {
        LOG_D("Linked demux src_{} → {}", stream_idx, GST_ELEMENT_NAME(next));
        ctx->tails["stream_" + std::to_string(stream_idx)] = next;
    }
    gst_object_unref(sink_pad);
}
```

## 5. Queue Insertion — `queue: {}` Pattern

Trong YAML config, bất kỳ element nào có thêm key `queue: {}` sẽ được tự động insert một `GstQueue` trước nó. Đây là cách tách biệt processing threads trong GStreamer.

```yaml
# YAML: queue: {} = dùng queue_defaults settings
processing:
  elements:
    - id: "pgie"
      queue: {}          # Insert queue với default settings
      ...

    # Hoặc override queue params:
    - id: "tracker"
      queue:
        max_size_buffers: 20
        leaky: 2
      ...
```

```cpp
// Xử lý trong PipelineBuilder
bool should_insert_queue(const ProcessingElementConfig& elem,
                         const QueueDefaults& defaults) {
    return elem.has_queue_config();  // true nếu YAML chứa "queue: ..."
}

GstElement* build_queue_for(const ProcessingElementConfig& elem,
                             const QueueDefaults& defaults,
                             GstElement* pipeline) {
    const auto& q_cfg = elem.queue_config.value_or(defaults);

    auto q = make_gst_element("queue", (elem.id + "_prequeue").c_str());
    if (!q) return nullptr;

    g_object_set(G_OBJECT(q.get()),
        "max-size-buffers",  (guint)q_cfg.max_size_buffers,
        "max-size-bytes",    (guint)(q_cfg.max_size_bytes_mb * 1024 * 1024),
        "max-size-time",     (guint64)(q_cfg.max_size_time_sec * GST_SECOND),
        "leaky",             leaky_mode_from_string(q_cfg.leaky),
        "silent",            (gboolean)q_cfg.silent,
        nullptr);

    gst_bin_add(GST_BIN(pipeline), q.get());
    return q.release();
}
```

## 6. Tee Element — Multiple Outputs

Khi một stream cần đi vào **nhiều sinks** (vừa display vừa record), cần insert `tee`:

```
...nvdsosd.src → tee → queue → nvv4l2h264enc → rtspclientsink
                     ↘ queue → nvv4l2h264enc → filesink
```

```cpp
// Link tee với 2 outputs
GstElement* tee = gst_element_factory_make("tee", "output_tee_0");
gst_bin_add(GST_BIN(pipeline), tee);
linker_->link(osd_element, tee);

// Branch 1: RTSP sink
auto* q1 = make_queue("rtsp_q_0");
auto* enc1 = make_h264_encoder("enc_rtsp_0");
auto* rtsp_sink = make_rtsp_sink("rtsp_0");
gst_bin_add_many(GST_BIN(pipeline), q1, enc1, rtsp_sink, nullptr);
linker_->link_chain({tee, q1, enc1, rtsp_sink});

// Branch 2: File sink
auto* q2 = make_queue("file_q_0");
auto* enc2 = make_h264_encoder("enc_file_0");
auto* file_sink = make_file_sink("file_0");
gst_bin_add_many(GST_BIN(pipeline), q2, enc2, file_sink, nullptr);
linker_->link_chain({tee, q2, enc2, file_sink});
```

Lưu ý: `tee` element tự động tạo src pads với pattern `src_%u` khi một sink được linked.

## 7. RAII cho GStreamer Objects

Mọi GstElement\* được tạo bởi `gst_element_factory_make()` phải được quản lý qua RAII helpers:

```cpp
#include "engine/core/utils/gst_utils.hpp"

// ─── Element guard ─────────────────────────────────────────────────────
auto elem = engine::core::utils::make_gst_element("nvinfer", "pgie");
// Nếu build fail, elem.get() = nullptr (factory_make failed)
// Khi ra khỏi scope, tự động gst_object_unref nếu chưa release

if (!configure_infer(elem.get(), config)) {
    // elem tự unref khi return
    return nullptr;
}

// Sau khi gst_bin_add() → bin owns → disarm guard
if (!gst_bin_add(GST_BIN(pipeline), elem.get())) return nullptr;
return elem.release();  // Transfer ownership, disarm RAII guard

// ─── Pad guard ─────────────────────────────────────────────────────────
engine::core::utils::GstPadPtr src_pad(
    gst_element_get_static_pad(element, "src"), gst_object_unref);
// ... use src_pad.get() ...
// gst_object_unref được gọi tự động khi ra scope

// ─── Caps guard ────────────────────────────────────────────────────────
engine::core::utils::GstCapsPtr caps(
    gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "NV12", nullptr),
    gst_caps_unref);
```

> Xem chi tiết RAII → [../RAII.md](../RAII.md)

## 8. Debugging Links

```bash
# Export DOT graph để visualize pipeline topology
GST_DEBUG_DUMP_DOT_DIR=dev/logs ./build/bin/vms_engine -c configs/default.yml

# Convert → hình ảnh
dot -Tpng dev/logs/*.dot -o pipeline.png

# Kiểm tra link failures trong log
./build/bin/vms_engine -c configs/default.yml 2>&1 | grep -E "(link|Link|LINK)"
```

### Common Link Errors

| Lỗi                               | Nguyên nhân                                   | Fix                                                                |
| --------------------------------- | --------------------------------------------- | ------------------------------------------------------------------ |
| `gst_element_link FAILED`         | Caps không tương thích                        | Thêm `capsfilter` hoặc `nvvideoconvert`                            |
| `Could not link src to sink`      | Element thứ tự sai                            | Kiểm tra `add` trước `link`                                        |
| pad-added callback không gọi      | nvstreamdemux signal connect sai              | Verify `g_signal_connect` timing (phải trước `set_state(PLAYING)`) |
| Deadlock khi linking dynamic pads | Link trong callback đang trên pipeline thread | Dùng `post_pad_link_message` để defer                              |
