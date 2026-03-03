#include "engine/pipeline/pipeline_manager.hpp"
#include "engine/pipeline/probes/probe_handler_manager.hpp"
#include "engine/core/utils/logger.hpp"
#include "engine/core/utils/gst_utils.hpp"

namespace engine::pipeline {

PipelineManager::PipelineManager(std::unique_ptr<engine::core::builders::IPipelineBuilder> builder)
    : builder_(std::move(builder)) {}

PipelineManager::~PipelineManager() {
    cleanup();
}

void PipelineManager::set_message_producer(engine::core::messaging::IMessageProducer* producer) {
    producer_ = producer;
}

bool PipelineManager::initialize(const engine::core::config::PipelineConfig& config) {
    if (state_ != engine::core::pipeline::PipelineState::Uninitialized) {
        LOG_W("PipelineManager already initialized");
        return false;
    }

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

    // Attach pad probes from event_handlers config
    if (!config.event_handlers.empty()) {
        probe_manager_ = std::make_unique<probes::ProbeHandlerManager>(pipeline_);
        if (!probe_manager_->attach_probes(config, producer_)) {
            LOG_E("Failed to attach one or more pad probes");
            // Non-fatal — continue with reduced functionality
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

    state_ = engine::core::pipeline::PipelineState::Playing;
    LOG_I("Pipeline set to PLAYING");
    return true;
}

bool PipelineManager::stop() {
    if (state_ == engine::core::pipeline::PipelineState::Stopped ||
        state_ == engine::core::pipeline::PipelineState::Uninitialized) {
        return true;
    }

    if (loop_ && g_main_loop_is_running(loop_)) {
        g_main_loop_quit(loop_);
    }

    if (loop_thread_.joinable()) {
        loop_thread_.join();
    }

    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
    }

    state_ = engine::core::pipeline::PipelineState::Stopped;
    LOG_I("Pipeline stopped");
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

engine::core::pipeline::PipelineState PipelineManager::get_state() const {
    return state_;
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
    if (loop_ && g_main_loop_is_running(loop_)) {
        g_main_loop_quit(loop_);
    }
}

void PipelineManager::cleanup() {
    // Detach probes before tearing down the pipeline
    if (probe_manager_) {
        probe_manager_->detach_all();
        probe_manager_.reset();
    }

    stop();

    if (pipeline_) {
        // Remove bus watch before unref
        engine::core::utils::GstBusPtr bus(gst_pipeline_get_bus(GST_PIPELINE(pipeline_)),
                                           gst_object_unref);
        if (bus) {
            gst_bus_remove_watch(bus.get());
        }
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }

    if (loop_) {
        g_main_loop_unref(loop_);
        loop_ = nullptr;
    }

    state_ = engine::core::pipeline::PipelineState::Uninitialized;
}

}  // namespace engine::pipeline
