# 02. Core Interfaces & Contracts

## 1. Tổng quan

Core layer (`core/`) định nghĩa các **pure abstract interfaces** (contracts) mà các layer khác phải tuân thủ. Core **không phụ thuộc** vào bất kỳ external library cụ thể nào — không có DeepStream headers, không có yaml-cpp, không có spdlog trong interface definitions.

```
┌─────────────────────────────────────────────────────────────┐
│                     CORE LAYER (Ports)                       │
│  IPipelineManager │ IPipelineBuilder │ IBuilderFactory       │
│  IElementBuilder  │ IConfigParser    │ IStorageManager       │
│  IMessageProducer │ IEventHandler    │ IProbeHandler         │
│  Logger macros    │ Config types     │ PipelineState enum    │
└─────────────────────────────────────────────────────────────┘
                              │
                              │ implements
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                   PIPELINE LAYER                             │
│  PipelineManager  │ PipelineBuilder  │ BuilderFactory        │
│  *Builder classes │ LinkManager      │ ProbeHandlerManager   │
└─────────────────────────────────────────────────────────────┘
```

## 2. IPipelineManager

**File**: `core/include/engine/core/pipeline/ipipeline_manager.hpp`

Quản lý **toàn bộ lifecycle** của GStreamer pipeline.

```cpp
namespace engine::core::pipeline {

enum class PipelineState {
    Uninitialized,  // Chưa gọi initialize()
    Ready,          // Pipeline đã build, sẵn sàng start
    Playing,        // Đang chạy
    Paused,         // Tạm dừng (frames buffered)
    Stopped,        // Đã dừng, resources released
    Error           // Lỗi không thể recover
};

struct PipelineInfo {
    std::string id;
    std::string name;
    PipelineState state;
    std::string last_error;
    int64_t uptime_seconds;
};

class IPipelineManager {
public:
    virtual ~IPipelineManager() = default;

    // ── Lifecycle ──────────────────────────────────────────────
    /// Build pipeline từ config, register trên GMainLoop.
    virtual bool initialize(engine::core::config::PipelineConfig& config,
                            GMainLoop* main_loop_context = nullptr) = 0;

    /// Đăng ký custom event handlers (từ config.custom_handlers hoặc plugins).
    virtual bool register_event_handlers(
        std::vector<engine::core::config::CustomHandlerConfig>& handlers) = 0;

    /// Set pipeline sang PLAYING state.
    virtual bool start() = 0;

    /// Dừng pipeline, giải phóng GStreamer resources.
    virtual bool stop() = 0;

    /// Tạm dừng pipeline (frames bị buffer).
    virtual bool pause() = 0;

    // ── State Query ────────────────────────────────────────────
    virtual PipelineState get_state() const = 0;
    virtual PipelineInfo get_info() const = 0;

    /// Truy cập GstPipeline thô (dùng cẩn thận, ownership giữ bởi manager).
    virtual GstElement* get_gst_pipeline() const = 0;
};

} // namespace engine::core::pipeline
```

### Implementation: `PipelineManager`

```cpp
// pipeline/include/engine/pipeline/pipeline_manager.hpp
namespace engine::pipeline {

class PipelineManager final : public engine::core::pipeline::IPipelineManager {
public:
    explicit PipelineManager(
        std::shared_ptr<engine::core::builders::IPipelineBuilder> builder = nullptr,
        std::shared_ptr<HandlerManager> handler_manager = nullptr);

private:
    std::shared_ptr<engine::core::builders::IPipelineBuilder>  builder_;
    GstElement*          pipeline_ = nullptr;
    guint                bus_watch_id_ = 0;
    GMainLoop*           main_loop_  = nullptr;
    std::atomic<PipelineState> state_{PipelineState::Uninitialized};
    mutable std::mutex   error_mutex_;
    std::string          last_error_;

    std::shared_ptr<HandlerManager>      handler_manager_;
    std::shared_ptr<ProbeHandlerManager> probe_manager_;

    static gboolean bus_cb(GstBus* bus, GstMessage* msg, gpointer user_data);
    gboolean handle_bus_message(GstBus* bus, GstMessage* msg);
};

} // namespace engine::pipeline
```

## 3. IPipelineBuilder

**File**: `core/include/engine/core/builders/ipipeline_builder.hpp`

Điều phối việc **xây dựng toàn bộ GStreamer pipeline** từ `PipelineConfig`.

```cpp
namespace engine::core::builders {

class IPipelineBuilder {
public:
    virtual ~IPipelineBuilder() = default;

    /**
     * @brief Build GStreamer pipeline từ configuration.
     *
     * @param config Full pipeline configuration
     * @param main_loop GMainLoop context (dùng cho async linking)
     * @return true nếu thành công
     *
     * Implementation phải:
     *  1. Tạo GstPipeline
     *  2. Gọi từng block builder theo thứ tự (phases)
     *  3. Link elements qua LinkManager
     *  4. Export DOT graph nếu config yêu cầu
     */
    virtual bool build(const engine::core::config::PipelineConfig& config,
                       GMainLoop* main_loop) = 0;

    /// Lấy GstPipeline đã build (null nếu chưa build hoặc build thất bại).
    virtual GstElement* get_pipeline() const = 0;
};

} // namespace engine::core::builders
```

### Implementation: `PipelineBuilder`

```cpp
// pipeline/include/engine/pipeline/block_builders/pipeline_builder.hpp
class PipelineBuilder : public engine::core::builders::IPipelineBuilder {
private:
    GstElement*   pipeline_ = nullptr;
    std::shared_ptr<engine::core::builders::IBuilderFactory> factory_;
    std::shared_ptr<LinkManager> link_manager_;
    std::map<std::string, GstElement*> tails_;  // upstream endpoint tracking

public:
    bool build(const engine::core::config::PipelineConfig& config,
               GMainLoop* main_loop) override;
    GstElement* get_pipeline() const override { return pipeline_; }
};
```

## 4. IBuilderFactory

**File**: `core/include/engine/core/builders/ibuilder_factory.hpp`

Factory tạo các **typed IElementBuilder instances**. Quan trọng: factory chỉ tạo builder, **không** truyền config khi tạo — config được truyền khi gọi `build()` (Full Config Pattern).

```cpp
namespace engine::core::builders {

class IBuilderFactory {
public:
    virtual ~IBuilderFactory() = default;

    // Sources
    virtual std::unique_ptr<IElementBuilder> create_source_builder() = 0;

    // Processing elements (xác định bởi role string)
    // role: "primary_inference" | "secondary_inference" | "tracker" | "demuxer"
    virtual std::unique_ptr<IElementBuilder> create_processing_builder(
        const std::string& role) = 0;

    // Visual elements
    // role: "tiler" | "osd"
    virtual std::unique_ptr<IElementBuilder> create_visual_builder(
        const std::string& role) = 0;

    // Output sinks
    // type: "display" | "file" | "rtsp" | "fake"
    virtual std::unique_ptr<IElementBuilder> create_sink_builder(
        const std::string& type) = 0;

    // Encoder
    // codec: "h264" | "h265"
    virtual std::unique_ptr<IElementBuilder> create_encoder_builder(
        const std::string& codec) = 0;

    // Standalone components
    virtual std::unique_ptr<IElementBuilder> create_smart_record_builder() = 0;
    virtual std::unique_ptr<IElementBuilder> create_msgbroker_builder() = 0;
    virtual std::unique_ptr<IElementBuilder> create_analytics_builder() = 0;
    virtual std::unique_ptr<IElementBuilder> create_queue_builder() = 0;
};

} // namespace engine::core::builders
```

### Implementation: `BuilderFactory`

```cpp
// pipeline/include/engine/pipeline/builder_factory.hpp
class BuilderFactory final : public engine::core::builders::IBuilderFactory {
public:
    std::unique_ptr<IElementBuilder> create_processing_builder(
        const std::string& role) override {
        if (role == "primary_inference" || role == "secondary_inference")
            return std::make_unique<InferBuilder>();
        if (role == "tracker")
            return std::make_unique<TrackerBuilder>();
        if (role == "demuxer")
            return std::make_unique<DemuxerBuilder>();
        LOG_E("Unknown processing role: {}", role);
        return nullptr;
    }
    // ... các create_* khác
};
```

## 5. IElementBuilder

**File**: `core/include/engine/core/builders/ielement_builder.hpp`

Interface cho việc **xây dựng một GstElement / GstBin cụ thể**.

```cpp
namespace engine::core::builders {

class IElementBuilder {
public:
    virtual ~IElementBuilder() = default;

    /**
     * @brief Build GstElement từ full pipeline configuration.
     *
     * Full Config Pattern: builder nhận toàn bộ PipelineConfig thay vì
     * slice nhỏ. Builder tự tìm phần config liên quan dựa trên id + role.
     *
     * @param config Full pipeline configuration
     * @param element_id ID của element cần build (khớp với config.id)
     * @param parent_bin GstBin chứa element (thường là GstPipeline)
     * @return GstElement* đã được configure; nullptr nếu lỗi
     *
     * Caller (block builder) chịu trách nhiệm:
     *   - Thêm element vào pipeline/bin (gst_bin_add)
     *   - Link element với elements khác
     */
    virtual GstElement* build(
        const engine::core::config::PipelineConfig& config,
        const std::string& element_id,
        GstElement* parent_bin) = 0;
};

} // namespace engine::core::builders
```

### Ví dụ: `InferBuilder`

```cpp
// pipeline/include/engine/pipeline/builders/infer_builder.hpp
class InferBuilder : public engine::core::builders::IElementBuilder {
public:
    GstElement* build(const engine::core::config::PipelineConfig& config,
                      const std::string& element_id,
                      GstElement* parent_bin) override {
        // 1. Tìm config cho element_id
        const auto* infer_cfg = find_processing_element(config, element_id);
        if (!infer_cfg) {
            LOG_E("No config found for element: {}", element_id);
            return nullptr;
        }

        // 2. Chọn GStreamer element type
        const char* type = (infer_cfg->type == "nvinferserver")
                           ? "nvinferserver" : "nvinfer";

        // 3. Tạo element
        auto elem = engine::core::utils::make_gst_element(type, element_id.c_str());
        if (!elem) return nullptr;

        // 4. Set properties
        g_object_set(G_OBJECT(elem.get()),
            "config-file-path", infer_cfg->config_file.c_str(),
            "unique-id",        infer_cfg->unique_id,
            "process-mode",     infer_cfg->process_mode,
            "batch-size",       infer_cfg->batch_size,
            "gpu-id",           infer_cfg->gpu_id,
            nullptr);

        if (infer_cfg->interval > 0) {
            g_object_set(G_OBJECT(elem.get()),
                "interval", (guint)infer_cfg->interval, nullptr);
        }

        // 5. Add to bin (bin takes ownership → release guard)
        if (!gst_bin_add(GST_BIN(parent_bin), elem.get())) {
            LOG_E("gst_bin_add failed for: {}", element_id);
            return nullptr;
        }

        return elem.release();
    }
};
```

## 6. Configuration Types

**File**: `core/include/engine/core/config/config_types.hpp`

```cpp
namespace engine::core::config {

struct PipelineConfig {
    std::string version = "1.0.0";

    // Pipeline metadata
    struct PipelineMeta {
        std::string id;
        std::string name;
        std::string log_level = "INFO";
        std::string gst_log_level = "*:1";
        std::string dot_file_dir;
        std::string log_file;
    } pipeline;

    // Queue defaults (cho queue: {} shorthand)
    struct QueueDefaults {
        int    max_size_buffers = 10;
        int    max_size_bytes_mb = 20;
        double max_size_time_sec = 0.5;
        std::string leaky = "downstream";
        bool   silent = true;
    } queue_defaults;

    SourceConfig    sources;                // nvmultiurisrcbin config
    std::vector<ProcessingElementConfig> processing;  // PGIE, SGIE, tracker, demuxer
    VisualsConfig   visuals;                // tiler + OSD
    std::vector<OutputConfig> outputs;      // sinks

    std::optional<SmartRecordConfig>       smart_record;
    std::optional<MessageBrokerConfig>     message_broker;
    std::optional<AnalyticsConfig>         analytics;
    std::optional<RestApiConfig>           rest_api;

    std::vector<StorageTargetConfig>   storage_configurations;
    std::vector<ExternalServiceConfig> external_services;
    std::vector<CustomHandlerConfig>   custom_handlers;
};

} // namespace engine::core::config
```

**Khác biệt so với lantanav2**: Không dùng `std::variant<DeepStreamInferenceConfig, DLStreamerInferenceConfig>`. Vì vms-engine là **DeepStream-native**, `ProcessingElementConfig` chứa trực tiếp DeepStream properties.

## 7. Logger Interface

**File**: `core/include/engine/core/utils/logger.hpp`

Global logging macros — dùng xuyên suốt codebase. **Luôn dùng `LOG_*` với underscore** (không phải `LOG*` như lantanav2).

```cpp
// Sử dụng LOG_* macros (với underscore — namespace engine::)
LOG_T("Trace: very verbose");               // spdlog::trace
LOG_D("Debug: building {}", elem_name);     // spdlog::debug
LOG_I("Info: pipeline started, n={}", n);   // spdlog::info
LOG_W("Warning: deprecated field '{}'", k); // spdlog::warn
LOG_E("Error: null pipeline");              // spdlog::error
LOG_C("Critical: cannot recover");          // spdlog::critical

// Khởi tạo logger từ config
engine::core::utils::initialize_logger(config);
// Sau đó LOG_* có thể dùng bất cứ nơi nào, kể cả nhiều threads
```

## 8. Interface Relationships

```
IPipelineManager
     │ uses (composition)
     ▼
IPipelineBuilder ──────────────────────────► IBuilderFactory
     │                                             │ creates
     │                                             ▼
     │                                       IElementBuilder
     │                                             │ builds
     ▼                                             ▼
GstPipeline* ◄─────── GstElement* (được add vào pipeline)


IPipelineManager
     │ uses (registration)
     ▼
IEventHandler ──── signal-connected-to ───► appsink / nvdssmartrecordbin
IProbeHandler ──── pad-probe-on ──────────► nvinfer / nvtracker src pads
```

## 9. IProbeHandler

**File**: `core/include/engine/core/probes/iprobe_handler.hpp`

```cpp
namespace engine::core::probes {

class IProbeHandler {
public:
    virtual ~IProbeHandler() = default;

    /// Attach probe lên pad của element
    virtual bool attach(GstElement* element,
                        const std::string& pad_name,
                        GstPadProbeType probe_type) = 0;

    /// Detach probe
    virtual void detach() = 0;

    /// Callback được GStreamer gọi khi buffer đi qua
    virtual GstPadProbeReturn on_buffer(GstPad* pad,
                                        GstPadProbeInfo* info) = 0;

    virtual std::string name() const = 0;
};

} // namespace engine::core::probes
```

## 10. IEventHandler

**File**: `core/include/engine/core/eventing/ievent_handler.hpp`

```cpp
namespace engine::core::eventing {

class IEventHandler {
public:
    virtual ~IEventHandler() = default;

    /// Connect signals lên GstElement (thường là appsink)
    virtual bool connect(GstElement* element,
                        const engine::core::config::CustomHandlerConfig& config) = 0;

    /// Disconnect signals, cleanup
    virtual void disconnect() = 0;

    virtual std::string name() const = 0;
};

} // namespace engine::core::eventing
```
