#include "engine/pipeline/pipeline_manager.hpp"
#include "engine/pipeline/evidence/evidence_request_service.hpp"
#include "engine/pipeline/evidence/frame_evidence_cache.hpp"
#include "engine/pipeline/probes/probe_handler_manager.hpp"
#include "engine/pipeline/runtime_stream_manager.hpp"
#include "engine/core/messaging/imessage_consumer.hpp"
#include "engine/core/utils/logger.hpp"
#include "engine/core/utils/gst_utils.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <future>
#include <set>
#include <sstream>

using engine::core::pipeline::RuntimeSourceErrorCode;
using engine::core::pipeline::RuntimeSourceMutationResult;

namespace engine::pipeline {

namespace {

namespace fs = std::filesystem;

struct SetParamRequest {
    GstElement* pipeline = nullptr;
    std::string element_id;
    std::string property;
    std::string value;
    std::promise<bool> promise;
};

struct AddSourceRequest {
    RuntimeStreamManager* manager = nullptr;
    GstElement* pipeline = nullptr;
    engine::core::config::SourcesConfig sources_config;
    engine::core::config::CameraConfig camera;
    std::string dot_file_dir;
    std::promise<RuntimeSourceMutationResult> promise;
};

struct RemoveSourceRequest {
    RuntimeStreamManager* manager = nullptr;
    GstElement* pipeline = nullptr;
    engine::core::config::SourcesConfig sources_config;
    std::string camera_id;
    std::string dot_file_dir;
    std::promise<RuntimeSourceMutationResult> promise;
};

struct GetParamRequest {
    GstElement* pipeline = nullptr;
    std::string element_id;
    std::string property;
    std::promise<std::string> promise;
};

RuntimeSourceMutationResult make_source_result(bool success, int http_status,
                                               RuntimeSourceErrorCode error_code,
                                               const std::string& camera_id,
                                               const std::string& message) {
    RuntimeSourceMutationResult result;
    result.success = success;
    result.http_status = http_status;
    result.error_code = error_code;
    result.camera_id = camera_id;
    result.message = message;
    return result;
}

std::string sanitize_name_component(const std::string& value) {
    std::string sanitized;
    sanitized.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch) != 0U) {
            sanitized.push_back(static_cast<char>(ch));
        } else {
            sanitized.push_back('_');
        }
    }
    if (sanitized.empty()) {
        sanitized = "camera";
    }
    return sanitized;
}

void request_encoder_keyframe(GObject* object, const char* property_name) {
    GParamSpec* spec = g_object_class_find_property(G_OBJECT_GET_CLASS(object), property_name);
    if (spec == nullptr || (spec->flags & G_PARAM_WRITABLE) == 0 ||
        G_PARAM_SPEC_VALUE_TYPE(spec) != G_TYPE_BOOLEAN) {
        return;
    }

    g_object_set(object, property_name, static_cast<gboolean>(TRUE), nullptr);
}

void request_keyframe_on_encoders(GstElement* pipeline) {
    if (pipeline == nullptr) {
        return;
    }

    GstIterator* iterator = gst_bin_iterate_recurse(GST_BIN(pipeline));
    if (iterator == nullptr) {
        return;
    }

    GValue value = G_VALUE_INIT;
    while (gst_iterator_next(iterator, &value) == GST_ITERATOR_OK) {
        auto* element = GST_ELEMENT(g_value_get_object(&value));
        GstElementFactory* factory =
            element != nullptr ? gst_element_get_factory(element) : nullptr;
        const gchar* factory_name =
            factory != nullptr ? gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)) : nullptr;

        if (factory_name != nullptr && (std::strcmp(factory_name, "nvv4l2h264enc") == 0 ||
                                        std::strcmp(factory_name, "nvv4l2h265enc") == 0)) {
            request_encoder_keyframe(G_OBJECT(element), "force-keyframe");
            request_encoder_keyframe(G_OBJECT(element), "force-idr");
            request_encoder_keyframe(G_OBJECT(element), "force-intra");
        }

        g_value_reset(&value);
    }

    g_value_unset(&value);
    gst_iterator_free(iterator);
}

void refresh_pipeline_after_source_mutation(GstElement* pipeline,
                                            const engine::core::config::SourcesConfig& sources) {
    if (pipeline == nullptr) {
        return;
    }

    const std::string mux_name = sources.mux.id.empty() ? std::string("batch_mux") : sources.mux.id;
    GstElement* muxer = gst_bin_get_by_name(GST_BIN(pipeline), mux_name.c_str());
    if (muxer != nullptr) {
        gst_element_send_event(muxer, gst_event_new_reconfigure());
        gst_object_unref(muxer);
    }

    gst_bin_recalculate_latency(GST_BIN(pipeline));
    request_keyframe_on_encoders(pipeline);
}

void dump_pipeline_dot_snapshot(GstElement* pipeline, const std::string& dot_file_dir,
                                const std::string& prefix, RuntimeSourceMutationResult& result) {
    if (pipeline == nullptr || dot_file_dir.empty() || !result.success) {
        return;
    }

    try {
        fs::create_directories(dot_file_dir);
    } catch (const std::exception& ex) {
        result.dot_dump_warning =
            std::string("failed to prepare dot output directory: ") + ex.what();
        return;
    }

    std::set<std::string> before;
    try {
        for (const auto& entry : fs::directory_iterator(dot_file_dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            if (entry.path().extension() == ".dot") {
                before.insert(entry.path().filename().string());
            }
        }
    } catch (const std::exception& ex) {
        result.dot_dump_warning = std::string("failed to scan dot output directory: ") + ex.what();
        return;
    }

    gst_debug_bin_to_dot_file_with_ts(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, prefix.c_str());

    fs::path newest_path;
    fs::file_time_type newest_time;
    bool found_new = false;
    try {
        for (const auto& entry : fs::directory_iterator(dot_file_dir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".dot") {
                continue;
            }

            const std::string filename = entry.path().filename().string();
            if (before.find(filename) != before.end() ||
                filename.find(prefix) == std::string::npos) {
                continue;
            }

            const auto write_time = fs::last_write_time(entry.path());
            if (!found_new || write_time > newest_time) {
                newest_time = write_time;
                newest_path = entry.path();
                found_new = true;
            }
        }
    } catch (const std::exception& ex) {
        result.dot_dump_warning = std::string("failed to resolve dot snapshot path: ") + ex.what();
        return;
    }

    if (!found_new) {
        result.dot_dump_warning = "dot snapshot requested but no new .dot file was created";
        return;
    }

    result.dot_file_path = newest_path.string();
}

std::string normalize_property_name(std::string property) {
    std::replace(property.begin(), property.end(), '_', '-');
    return property;
}

std::string to_lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool parse_bool_value(const std::string& raw_value, gboolean& out_value) {
    const std::string lowered = to_lower_copy(raw_value);
    if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on") {
        out_value = TRUE;
        return true;
    }
    if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off") {
        out_value = FALSE;
        return true;
    }
    return false;
}

bool set_property_from_string(GObject* object, const std::string& property,
                              const std::string& raw_value) {
    if (object == nullptr) {
        return false;
    }

    GParamSpec* spec = g_object_class_find_property(G_OBJECT_GET_CLASS(object), property.c_str());
    if (spec == nullptr || (spec->flags & G_PARAM_WRITABLE) == 0) {
        return false;
    }

    GValue value = G_VALUE_INIT;
    g_value_init(&value, G_PARAM_SPEC_VALUE_TYPE(spec));

    bool converted = false;
    try {
        const GType value_type = G_PARAM_SPEC_VALUE_TYPE(spec);
        if (value_type == G_TYPE_BOOLEAN) {
            gboolean parsed = FALSE;
            converted = parse_bool_value(raw_value, parsed);
            if (converted) {
                g_value_set_boolean(&value, parsed);
            }
        } else if (value_type == G_TYPE_INT) {
            g_value_set_int(&value, std::stoi(raw_value));
            converted = true;
        } else if (value_type == G_TYPE_UINT) {
            g_value_set_uint(&value, static_cast<guint>(std::stoul(raw_value)));
            converted = true;
        } else if (value_type == G_TYPE_INT64) {
            g_value_set_int64(&value, std::stoll(raw_value));
            converted = true;
        } else if (value_type == G_TYPE_UINT64) {
            g_value_set_uint64(&value, static_cast<guint64>(std::stoull(raw_value)));
            converted = true;
        } else if (value_type == G_TYPE_FLOAT) {
            g_value_set_float(&value, std::stof(raw_value));
            converted = true;
        } else if (value_type == G_TYPE_DOUBLE) {
            g_value_set_double(&value, std::stod(raw_value));
            converted = true;
        } else if (value_type == G_TYPE_STRING) {
            g_value_set_string(&value, raw_value.c_str());
            converted = true;
        } else if (g_type_is_a(value_type, G_TYPE_ENUM)) {
            g_value_set_enum(&value, std::stoi(raw_value));
            converted = true;
        } else if (g_type_is_a(value_type, G_TYPE_FLAGS)) {
            g_value_set_flags(&value, static_cast<guint>(std::stoul(raw_value)));
            converted = true;
        }
    } catch (const std::exception&) {
        converted = false;
    }

    if (converted) {
        g_object_set_property(object, property.c_str(), &value);
    }

    g_value_unset(&value);
    return converted;
}

std::string get_property_as_string(GObject* object, const std::string& property) {
    if (object == nullptr) {
        return {};
    }

    GParamSpec* spec = g_object_class_find_property(G_OBJECT_GET_CLASS(object), property.c_str());
    if (spec == nullptr || (spec->flags & G_PARAM_READABLE) == 0) {
        return {};
    }

    GValue value = G_VALUE_INIT;
    g_value_init(&value, G_PARAM_SPEC_VALUE_TYPE(spec));
    g_object_get_property(object, property.c_str(), &value);

    std::string result;
    const GType value_type = G_PARAM_SPEC_VALUE_TYPE(spec);
    if (value_type == G_TYPE_BOOLEAN) {
        result = g_value_get_boolean(&value) ? "true" : "false";
    } else if (value_type == G_TYPE_INT) {
        result = std::to_string(g_value_get_int(&value));
    } else if (value_type == G_TYPE_UINT) {
        result = std::to_string(g_value_get_uint(&value));
    } else if (value_type == G_TYPE_INT64) {
        result = std::to_string(g_value_get_int64(&value));
    } else if (value_type == G_TYPE_UINT64) {
        result = std::to_string(g_value_get_uint64(&value));
    } else if (value_type == G_TYPE_FLOAT) {
        result = std::to_string(g_value_get_float(&value));
    } else if (value_type == G_TYPE_DOUBLE) {
        result = std::to_string(g_value_get_double(&value));
    } else if (value_type == G_TYPE_STRING) {
        const gchar* str = g_value_get_string(&value);
        result = str ? str : "";
    } else if (g_type_is_a(value_type, G_TYPE_ENUM)) {
        result = std::to_string(g_value_get_enum(&value));
    } else if (g_type_is_a(value_type, G_TYPE_FLAGS)) {
        result = std::to_string(g_value_get_flags(&value));
    }

    g_value_unset(&value);
    return result;
}

gboolean invoke_set_param(gpointer data) {
    std::unique_ptr<SetParamRequest> request(static_cast<SetParamRequest*>(data));
    if (request->pipeline == nullptr) {
        request->promise.set_value(false);
        return G_SOURCE_REMOVE;
    }

    GstElement* element =
        gst_bin_get_by_name(GST_BIN(request->pipeline), request->element_id.c_str());
    if (element == nullptr) {
        request->promise.set_value(false);
        return G_SOURCE_REMOVE;
    }

    const bool success =
        set_property_from_string(G_OBJECT(element), request->property, request->value);
    gst_object_unref(element);
    request->promise.set_value(success);
    return G_SOURCE_REMOVE;
}

gboolean invoke_get_param(gpointer data) {
    std::unique_ptr<GetParamRequest> request(static_cast<GetParamRequest*>(data));
    if (request->pipeline == nullptr) {
        request->promise.set_value({});
        return G_SOURCE_REMOVE;
    }

    GstElement* element =
        gst_bin_get_by_name(GST_BIN(request->pipeline), request->element_id.c_str());
    if (element == nullptr) {
        request->promise.set_value({});
        return G_SOURCE_REMOVE;
    }

    std::string value = get_property_as_string(G_OBJECT(element), request->property);
    gst_object_unref(element);
    request->promise.set_value(std::move(value));
    return G_SOURCE_REMOVE;
}

gboolean invoke_add_source(gpointer data) {
    std::unique_ptr<AddSourceRequest> request(static_cast<AddSourceRequest*>(data));
    RuntimeSourceMutationResult result =
        request->manager != nullptr
            ? request->manager->add_stream_detailed(request->camera)
            : make_source_result(false, 500, RuntimeSourceErrorCode::InternalError,
                                 request->camera.id, "runtime stream manager is unavailable");
    if (result.success) {
        refresh_pipeline_after_source_mutation(request->pipeline, request->sources_config);
        dump_pipeline_dot_snapshot(
            request->pipeline, request->dot_file_dir,
            std::string("runtime_add_") + sanitize_name_component(request->camera.id), result);
    }
    request->promise.set_value(std::move(result));
    return G_SOURCE_REMOVE;
}

gboolean invoke_remove_source(gpointer data) {
    std::unique_ptr<RemoveSourceRequest> request(static_cast<RemoveSourceRequest*>(data));
    RuntimeSourceMutationResult result =
        request->manager != nullptr
            ? request->manager->remove_stream_detailed(request->camera_id)
            : make_source_result(false, 500, RuntimeSourceErrorCode::InternalError,
                                 request->camera_id, "runtime stream manager is unavailable");
    if (result.success) {
        refresh_pipeline_after_source_mutation(request->pipeline, request->sources_config);
        dump_pipeline_dot_snapshot(
            request->pipeline, request->dot_file_dir,
            std::string("runtime_remove_") + sanitize_name_component(request->camera_id), result);
    }
    request->promise.set_value(std::move(result));
    return G_SOURCE_REMOVE;
}

}  // namespace

PipelineManager::PipelineManager(std::unique_ptr<engine::core::builders::IPipelineBuilder> builder)
    : builder_(std::move(builder)) {}

PipelineManager::~PipelineManager() {
    cleanup();
}

void PipelineManager::set_message_producer(engine::core::messaging::IMessageProducer* producer) {
    producer_ = producer;
}

void PipelineManager::set_message_consumer(engine::core::messaging::IMessageConsumer* consumer) {
    consumer_ = consumer;
}

bool PipelineManager::initialize(const engine::core::config::PipelineConfig& config) {
    if (state_ != engine::core::pipeline::PipelineState::Uninitialized) {
        LOG_W("PipelineManager already initialized");
        return false;
    }

    config_ = config;

    // Create GMainLoop
    loop_ = g_main_loop_new(nullptr, FALSE);
    if (!loop_) {
        LOG_E("Failed to create GMainLoop");
        return false;
    }

    // Delegate pipeline construction to builder
    if (!builder_->build(config, loop_)) {
        LOG_E("Pipeline build failed");
        g_main_loop_unref(loop_);
        loop_ = nullptr;
        return false;
    }

    pipeline_ = builder_->get_pipeline();
    if (!pipeline_) {
        LOG_E("Builder returned null pipeline");
        g_main_loop_unref(loop_);
        loop_ = nullptr;
        return false;
    }

    // Attach bus watch
    engine::core::utils::GstBusPtr bus(gst_pipeline_get_bus(GST_PIPELINE(pipeline_)),
                                       gst_object_unref);
    if (bus) {
        gst_bus_add_watch(bus.get(), on_bus_message, this);
    }

    if (config.evidence && config.evidence->enable) {
        frame_evidence_cache_ = std::make_unique<evidence::FrameEvidenceCache>(*config.evidence);
    }

    if (config.evidence && config.evidence->enable && producer_ && consumer_) {
        evidence_request_service_ = std::make_unique<evidence::EvidenceRequestService>(
            *config.evidence, producer_, frame_evidence_cache_.get());
        LOG_I(
            "PipelineManager: evidence subsystem initialized (request='{}' ready='{}' "
            "save_dir='{}')",
            config.evidence->request_channel, config.evidence->ready_channel,
            config.evidence->save_dir);
    } else if (config.evidence && config.evidence->enable) {
        LOG_W("PipelineManager: evidence enabled in config but producer/consumer not fully wired");
    }

    // Attach pad probes from event_handlers config
    if (!config.event_handlers.empty()) {
        probe_manager_ = std::make_unique<probes::ProbeHandlerManager>(pipeline_);
        if (!probe_manager_->attach_probes(config, producer_, frame_evidence_cache_.get())) {
            LOG_E("Failed to attach one or more pad probes");
            // Non-fatal — continue with reduced functionality
        }
    }

    if (config.sources.type == "nvurisrcbin") {
        const std::string source_root_name =
            config.sources.id.empty() ? std::string("sources_bin") : config.sources.id;
        const std::string mux_name =
            config.sources.mux.id.empty() ? std::string("batch_mux") : config.sources.mux.id;
        GstElement* source_root = gst_bin_get_by_name(GST_BIN(pipeline_), source_root_name.c_str());
        GstElement* muxer = gst_bin_get_by_name(GST_BIN(pipeline_), mux_name.c_str());
        if (source_root != nullptr && muxer != nullptr) {
            auto sources_config = config.sources;
            if (!config.pipeline.id.empty()) {
                sources_config.smart_rec_file_prefix = config.pipeline.id;
            }

            runtime_stream_manager_ =
                std::make_unique<RuntimeStreamManager>(source_root, muxer, sources_config);
            LOG_I("PipelineManager: runtime stream manager enabled for manual sources");
        } else {
            LOG_W(
                "PipelineManager: manual source runtime manager unavailable (source_root={}, "
                "muxer={})",
                source_root != nullptr ? "ok" : "missing", muxer != nullptr ? "ok" : "missing");
        }

        if (source_root != nullptr) {
            gst_object_unref(source_root);
        }
        if (muxer != nullptr) {
            gst_object_unref(muxer);
        }
    }

    state_ = engine::core::pipeline::PipelineState::Ready;
    LOG_I("PipelineManager initialized (state=Ready)");
    return true;
}

bool PipelineManager::start() {
    if (state_ != engine::core::pipeline::PipelineState::Ready &&
        state_ != engine::core::pipeline::PipelineState::Paused) {
        LOG_W("Cannot start from current state");
        return false;
    }

    GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);

    if (ret == GST_STATE_CHANGE_FAILURE) {
        LOG_E("Failed to set pipeline to PLAYING");
        state_ = engine::core::pipeline::PipelineState::Error;
        return false;
    }

    // Run GMainLoop on a thread
    if (!loop_thread_.joinable()) {
        loop_thread_ = std::thread([this] { g_main_loop_run(loop_); });
    }

    if (evidence_request_service_ && consumer_ && config_.evidence) {
        if (!config_.evidence->request_channel.empty() &&
            consumer_->subscribe(config_.evidence->request_channel)) {
            evidence_request_service_->start();
            stop_evidence_.store(false);
            if (!evidence_thread_.joinable()) {
                evidence_thread_ = std::thread(&PipelineManager::evidence_loop, this);
            }
            LOG_I("PipelineManager: evidence loop started on '{}'",
                  config_.evidence->request_channel);
        } else {
            LOG_W("PipelineManager: evidence request channel subscription failed for '{}'",
                  config_.evidence->request_channel);
        }
    }

    state_ = engine::core::pipeline::PipelineState::Playing;
    LOG_I("Pipeline set to PLAYING");
    return true;
}

bool PipelineManager::stop() {
    if (state_ == engine::core::pipeline::PipelineState::Stopped ||
        state_ == engine::core::pipeline::PipelineState::Uninitialized) {
        LOG_I(
            "PipelineManager::stop(): no-op for state={} ",
            state_ == engine::core::pipeline::PipelineState::Stopped ? "Stopped" : "Uninitialized");
        return true;
    }

    LOG_I("PipelineManager::stop(): begin (state={})",
          state_ == engine::core::pipeline::PipelineState::Playing  ? "Playing"
          : state_ == engine::core::pipeline::PipelineState::Paused ? "Paused"
                                                                    : "Other");

    LOG_I("PipelineManager::stop(): stopping evidence loop");
    stop_evidence_loop();

    if (loop_ && g_main_loop_is_running(loop_)) {
        LOG_I("PipelineManager::stop(): quitting GMainLoop");
        g_main_loop_quit(loop_);
    }

    if (loop_thread_.joinable()) {
        LOG_I("PipelineManager::stop(): joining loop thread");
        loop_thread_.join();
    }

    if (pipeline_) {
        LOG_I("PipelineManager::stop(): setting pipeline to NULL");
        gst_element_set_state(pipeline_, GST_STATE_NULL);
    }

    state_ = engine::core::pipeline::PipelineState::Stopped;
    LOG_I("PipelineManager::stop(): complete");
    return true;
}

bool PipelineManager::pause() {
    if (state_ != engine::core::pipeline::PipelineState::Playing) {
        return false;
    }

    GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PAUSED);

    if (ret == GST_STATE_CHANGE_FAILURE) {
        LOG_E("Failed to pause pipeline");
        return false;
    }

    state_ = engine::core::pipeline::PipelineState::Paused;
    LOG_I("Pipeline paused");
    return true;
}

bool PipelineManager::resume() {
    if (state_ != engine::core::pipeline::PipelineState::Paused) {
        return false;
    }
    return start();
}

RuntimeSourceMutationResult PipelineManager::list_sources_detailed() {
    if (config_.sources.type != "nvurisrcbin") {
        return make_source_result(
            false, 422, RuntimeSourceErrorCode::UnsupportedSourceMode, "",
            "runtime source control is only available for sources.type=nvurisrcbin");
    }

    if (!runtime_stream_manager_) {
        return make_source_result(false, 500, RuntimeSourceErrorCode::InternalError, "",
                                  "runtime stream manager is unavailable");
    }

    RuntimeSourceMutationResult result =
        make_source_result(true, 200, RuntimeSourceErrorCode::None, "", "active sources listed");
    result.sources = runtime_stream_manager_->list_streams();
    result.active_source_count = static_cast<int>(result.sources.size());
    return result;
}

RuntimeSourceMutationResult PipelineManager::add_source_detailed(
    const engine::core::config::CameraConfig& camera) {
    if (config_.sources.type != "nvurisrcbin") {
        LOG_W("PipelineManager::add_source unavailable for sources.type='{}'",
              config_.sources.type);
        return make_source_result(
            false, 422, RuntimeSourceErrorCode::UnsupportedSourceMode, camera.id,
            "runtime source control is only available for sources.type=nvurisrcbin");
    }

    if (!runtime_stream_manager_) {
        LOG_W("PipelineManager::add_source runtime manager unavailable");
        return make_source_result(false, 500, RuntimeSourceErrorCode::InternalError, camera.id,
                                  "runtime stream manager is unavailable");
    }

    if (loop_ == nullptr || !g_main_loop_is_running(loop_)) {
        RuntimeSourceMutationResult result = runtime_stream_manager_->add_stream_detailed(camera);
        if (result.success) {
            refresh_pipeline_after_source_mutation(pipeline_, config_.sources);
            dump_pipeline_dot_snapshot(
                pipeline_, config_.pipeline.dot_file_dir,
                std::string("runtime_add_") + sanitize_name_component(camera.id), result);
        }
        return result;
    }

    auto* request = new AddSourceRequest{runtime_stream_manager_.get(), pipeline_, config_.sources,
                                         camera, config_.pipeline.dot_file_dir};
    auto future = request->promise.get_future();
    g_main_context_invoke(g_main_loop_get_context(loop_), invoke_add_source, request);
    if (future.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
        LOG_W("PipelineManager::add_source timeout for '{}'", camera.id);
        return make_source_result(false, 500, RuntimeSourceErrorCode::OperationTimeout, camera.id,
                                  "timed out while adding runtime camera");
    }

    return future.get();
}

RuntimeSourceMutationResult PipelineManager::remove_source_detailed(const std::string& camera_id) {
    if (config_.sources.type != "nvurisrcbin") {
        LOG_W("PipelineManager::remove_source unavailable for sources.type='{}'",
              config_.sources.type);
        return make_source_result(
            false, 422, RuntimeSourceErrorCode::UnsupportedSourceMode, camera_id,
            "runtime source control is only available for sources.type=nvurisrcbin");
    }

    if (!runtime_stream_manager_) {
        LOG_W("PipelineManager::remove_source runtime manager unavailable");
        return make_source_result(false, 500, RuntimeSourceErrorCode::InternalError, camera_id,
                                  "runtime stream manager is unavailable");
    }

    if (loop_ == nullptr || !g_main_loop_is_running(loop_)) {
        RuntimeSourceMutationResult result =
            runtime_stream_manager_->remove_stream_detailed(camera_id);
        if (result.success) {
            refresh_pipeline_after_source_mutation(pipeline_, config_.sources);
            dump_pipeline_dot_snapshot(
                pipeline_, config_.pipeline.dot_file_dir,
                std::string("runtime_remove_") + sanitize_name_component(camera_id), result);
        }
        return result;
    }

    auto* request =
        new RemoveSourceRequest{runtime_stream_manager_.get(), pipeline_, config_.sources,
                                camera_id, config_.pipeline.dot_file_dir};
    auto future = request->promise.get_future();
    g_main_context_invoke(g_main_loop_get_context(loop_), invoke_remove_source, request);
    if (future.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
        LOG_W("PipelineManager::remove_source timeout for '{}'", camera_id);
        return make_source_result(false, 500, RuntimeSourceErrorCode::OperationTimeout, camera_id,
                                  "timed out while removing runtime camera");
    }

    return future.get();
}

bool PipelineManager::add_source(const engine::core::config::CameraConfig& camera) {
    return add_source_detailed(camera).success;
}

bool PipelineManager::remove_source(const std::string& camera_id) {
    return remove_source_detailed(camera_id).success;
}

engine::core::pipeline::PipelineState PipelineManager::get_state() const {
    return state_;
}

bool PipelineManager::set_param(const std::string& element_id, const std::string& property,
                                const std::string& value) {
    if (pipeline_ == nullptr || loop_ == nullptr || element_id.empty() || property.empty()) {
        return false;
    }

    const std::string normalized_property = normalize_property_name(property);
    if (!g_main_loop_is_running(loop_)) {
        GstElement* element = gst_bin_get_by_name(GST_BIN(pipeline_), element_id.c_str());
        if (element == nullptr) {
            return false;
        }
        const bool success =
            set_property_from_string(G_OBJECT(element), normalized_property, value);
        gst_object_unref(element);
        return success;
    }

    auto* request = new SetParamRequest{pipeline_, element_id, normalized_property, value};
    auto future = request->promise.get_future();
    g_main_context_invoke(g_main_loop_get_context(loop_), invoke_set_param, request);
    if (future.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
        LOG_W("PipelineManager::set_param timeout for {}.{}", element_id, normalized_property);
        return false;
    }
    return future.get();
}

std::string PipelineManager::get_param(const std::string& element_id, const std::string& property) {
    if (pipeline_ == nullptr || loop_ == nullptr || element_id.empty() || property.empty()) {
        return {};
    }

    const std::string normalized_property = normalize_property_name(property);
    if (!g_main_loop_is_running(loop_)) {
        GstElement* element = gst_bin_get_by_name(GST_BIN(pipeline_), element_id.c_str());
        if (element == nullptr) {
            return {};
        }
        std::string value = get_property_as_string(G_OBJECT(element), normalized_property);
        gst_object_unref(element);
        return value;
    }

    auto* request = new GetParamRequest{pipeline_, element_id, normalized_property};
    auto future = request->promise.get_future();
    g_main_context_invoke(g_main_loop_get_context(loop_), invoke_get_param, request);
    if (future.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
        LOG_W("PipelineManager::get_param timeout for {}.{}", element_id, normalized_property);
        return {};
    }
    return future.get();
}

gboolean PipelineManager::on_bus_message(GstBus* /*bus*/, GstMessage* msg, gpointer data) {
    auto* self = static_cast<PipelineManager*>(data);

    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            self->handle_eos();
            return FALSE;

        case GST_MESSAGE_ERROR: {
            GError* err = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_error(msg, &err, &debug);
            self->handle_error(err, debug);
            g_error_free(err);
            g_free(debug);
            return FALSE;
        }

        case GST_MESSAGE_WARNING: {
            GError* err = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_warning(msg, &err, &debug);
            LOG_W("GStreamer warning: {} ({})", err->message, debug ? debug : "");
            g_error_free(err);
            g_free(debug);
            break;
        }

        case GST_MESSAGE_STATE_CHANGED:
            // Only log pipeline-level state changes
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(self->pipeline_)) {
                GstState old_state, new_state, pending;
                gst_message_parse_state_changed(msg, &old_state, &new_state, &pending);
                LOG_D("Pipeline state: {} → {}", gst_element_state_get_name(old_state),
                      gst_element_state_get_name(new_state));
            }
            break;

        default:
            break;
    }
    return TRUE;
}

void PipelineManager::handle_eos() {
    LOG_I("End-of-stream received");
    state_ = engine::core::pipeline::PipelineState::Stopped;
    if (loop_ && g_main_loop_is_running(loop_)) {
        g_main_loop_quit(loop_);
    }
}

void PipelineManager::handle_error(GError* err, const gchar* debug) {
    LOG_E("GStreamer error: {} ({})", err->message, debug ? debug : "");
    state_ = engine::core::pipeline::PipelineState::Error;
    stop_evidence_loop();
    if (loop_ && g_main_loop_is_running(loop_)) {
        g_main_loop_quit(loop_);
    }
}

void PipelineManager::evidence_loop() {
    while (!stop_evidence_.load()) {
        if (!consumer_ || !evidence_request_service_) {
            break;
        }

        engine::core::messaging::ConsumedMessage message;
        if (!consumer_->poll(250, message)) {
            continue;
        }

        if (!message.payload.empty()) {
            evidence_request_service_->enqueue_request(message.payload);
        }
        consumer_->ack(message);
    }
}

void PipelineManager::stop_evidence_loop() {
    stop_evidence_.store(true);
    if (evidence_request_service_) {
        evidence_request_service_->stop();
    }
    if (evidence_thread_.joinable()) {
        evidence_thread_.join();
    }
}

void PipelineManager::cleanup() {
    // Detach probes before tearing down the pipeline
    if (probe_manager_) {
        probe_manager_->detach_all();
        probe_manager_.reset();
    }

    runtime_stream_manager_.reset();

    stop_evidence_loop();

    stop();

    evidence_request_service_.reset();
    frame_evidence_cache_.reset();

    if (pipeline_) {
        // Pipeline ownership stays with PipelineBuilder; only remove the bus watch here.
        engine::core::utils::GstBusPtr bus(gst_pipeline_get_bus(GST_PIPELINE(pipeline_)),
                                           gst_object_unref);
        if (bus) {
            gst_bus_remove_watch(bus.get());
        }
        pipeline_ = nullptr;
    }

    if (loop_) {
        g_main_loop_unref(loop_);
        loop_ = nullptr;
    }

    state_ = engine::core::pipeline::PipelineState::Uninitialized;
}

}  // namespace engine::pipeline
