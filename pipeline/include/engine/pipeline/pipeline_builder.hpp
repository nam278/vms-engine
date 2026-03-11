#pragma once
#include "engine/core/builders/ipipeline_builder.hpp"
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>
#include <glib.h>
#include <string>
#include <unordered_map>

namespace engine::pipeline {

/**
 * @brief Concrete IPipelineBuilder for DeepStream pipelines.
 *
 * Orchestrates the 4-phase build sequence:
 *   Phase 1 – Source block  (nvmultiurisrcbin)
 *   Phase 2 – Processing    (nvinfer, nvtracker, nvdsanalytics, …)
 *   Phase 3 – Visuals       (nvmultistreamtiler, nvdsosd)
 *   Phase 4 – Outputs       (encoder → sink chains)
 *
 * After a successful build(), get_pipeline() returns the GstPipeline element
 * ready to be handed to PipelineManager for lifecycle management.
 */
class PipelineBuilder : public engine::core::builders::IPipelineBuilder {
   public:
    PipelineBuilder() = default;
    ~PipelineBuilder() override;

    /**
     * @brief Execute all build phases and populate the internal pipeline.
     * @param config    Full, parsed pipeline configuration.
     * @param main_loop GMainLoop owned by PipelineManager (passed through to
     *                  elements that need it, e.g. smart-record callbacks).
     * @return true when all phases succeed and the pipeline is ready to play.
     */
    bool build(const engine::core::config::PipelineConfig& config, GMainLoop* main_loop) override;

    /**
     * @brief Return the built GstPipeline element.
     *
     * Ownership remains with this builder; caller MUST NOT unref the pointer.
     * Returns nullptr if build() has not been called or failed.
     */
    GstElement* get_pipeline() const override;

   private:
    GstElement* pipeline_ = nullptr;
    GMainLoop* loop_ = nullptr;

    /// Tail-element registry: phase-name → last element in that branch.
    std::unordered_map<std::string, GstElement*> tails_;

    void cleanup_pipeline();
};

}  // namespace engine::pipeline
