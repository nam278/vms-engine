#include "engine/pipeline/pipeline_manager.hpp"
#include "engine/pipeline/evidence/evidence_request_service.hpp"
#include "engine/pipeline/evidence/frame_evidence_cache.hpp"
#include "engine/pipeline/probes/probe_handler_manager.hpp"
#include "engine/core/messaging/imessage_consumer.hpp"
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
