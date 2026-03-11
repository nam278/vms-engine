#pragma once
#include "engine/core/pipeline/ipipeline_manager.hpp"
#include "engine/core/runtime/iruntime_param_manager.hpp"
#include "engine/core/builders/ipipeline_builder.hpp"
#include "engine/core/config/config_types.hpp"

#include <atomic>
#include <gst/gst.h>
#include <memory>
#include <thread>

namespace engine::core::messaging {
class IMessageProducer;
class IMessageConsumer;
}  // namespace engine::core::messaging

namespace engine::pipeline {

namespace evidence {
class FrameEvidenceCache;
class EvidenceRequestService;
}  // namespace evidence

namespace probes {
class ProbeHandlerManager;
}

class RuntimeStreamManager;

/**
 * @brief Concrete IPipelineManager implementation.
 *
 * Owns the GstPipeline and GMainLoop. Delegates construction to an
 * injected IPipelineBuilder. Bus watch runs on a dedicated thread.
 * After build, attaches pad probes via ProbeHandlerManager.
 */
class PipelineManager : public engine::core::pipeline::IPipelineManager,
                        public engine::core::runtime::IRuntimeParamManager {
   public:
    explicit PipelineManager(std::unique_ptr<engine::core::builders::IPipelineBuilder> builder);

    ~PipelineManager() override;

    bool initialize(const engine::core::config::PipelineConfig& config) override;
    bool start() override;
    bool stop() override;
    bool pause() override;
    bool resume() override;
    bool add_source(const engine::core::config::CameraConfig& camera) override;
    bool remove_source(const std::string& camera_id) override;
    engine::core::pipeline::PipelineState get_state() const override;
    bool set_param(const std::string& element_id, const std::string& property,
                   const std::string& value) override;
    std::string get_param(const std::string& element_id, const std::string& property) override;

    /**
     * @brief Set optional message producer for probe handlers.
     *
     * Call before initialize(). If not set (or set to nullptr), probe
     * handlers that require messaging will silently skip publishing.
     *
     * @param producer  Borrowed pointer — caller retains ownership.
     */
    void set_message_producer(engine::core::messaging::IMessageProducer* producer);

    /**
     * @brief Set optional message consumer for evidence_request intake.
     *
     * Call before initialize(). If not set (or set to nullptr), evidence
     * request consumption is disabled even if `evidence:` exists in config.
     *
     * @param consumer Borrowed pointer — caller retains ownership.
     */
    void set_message_consumer(engine::core::messaging::IMessageConsumer* consumer);

   private:
    std::unique_ptr<engine::core::builders::IPipelineBuilder> builder_;
    engine::core::config::PipelineConfig config_;
    GstElement* pipeline_ = nullptr;
    GMainLoop* loop_ = nullptr;
    std::thread loop_thread_;
    std::thread evidence_thread_;
    std::atomic<bool> stop_evidence_{false};

    engine::core::pipeline::PipelineState state_{
        engine::core::pipeline::PipelineState::Uninitialized};

    /// Probe manager — created during initialize()
    std::unique_ptr<probes::ProbeHandlerManager> probe_manager_;

    /// Optional message producer for probe handlers (borrowed, not owned)
    engine::core::messaging::IMessageProducer* producer_ = nullptr;

    /// Optional message consumer for evidence_request messages (borrowed, not owned)
    engine::core::messaging::IMessageConsumer* consumer_ = nullptr;

    std::unique_ptr<evidence::FrameEvidenceCache> frame_evidence_cache_;
    std::unique_ptr<evidence::EvidenceRequestService> evidence_request_service_;
    std::unique_ptr<RuntimeStreamManager> runtime_stream_manager_;

    static gboolean on_bus_message(GstBus* bus, GstMessage* msg, gpointer data);
    void handle_eos();
    void handle_error(GError* err, const gchar* debug);
    void evidence_loop();
    void stop_evidence_loop();
    void cleanup();
};

}  // namespace engine::pipeline
