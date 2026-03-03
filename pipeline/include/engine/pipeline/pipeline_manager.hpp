#pragma once
#include "engine/core/pipeline/ipipeline_manager.hpp"
#include "engine/core/builders/ipipeline_builder.hpp"
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>
#include <memory>
#include <thread>

namespace engine::pipeline {

/**
 * @brief Concrete IPipelineManager implementation.
 *
 * Owns the GstPipeline and GMainLoop. Delegates construction to an
 * injected IPipelineBuilder. Bus watch runs on a dedicated thread.
 */
class PipelineManager : public engine::core::pipeline::IPipelineManager {
   public:
    explicit PipelineManager(std::unique_ptr<engine::core::builders::IPipelineBuilder> builder);

    ~PipelineManager() override;

    bool initialize(const engine::core::config::PipelineConfig& config) override;
    bool start() override;
    bool stop() override;
    bool pause() override;
    bool resume() override;
    engine::core::pipeline::PipelineState get_state() const override;

   private:
    std::unique_ptr<engine::core::builders::IPipelineBuilder> builder_;
    GstElement* pipeline_ = nullptr;
    GMainLoop* loop_ = nullptr;
    std::thread loop_thread_;

    engine::core::pipeline::PipelineState state_{
        engine::core::pipeline::PipelineState::Uninitialized};

    static gboolean on_bus_message(GstBus* bus, GstMessage* msg, gpointer data);
    void handle_eos();
    void handle_error(GError* err, const gchar* debug);
    void cleanup();
};

}  // namespace engine::pipeline
