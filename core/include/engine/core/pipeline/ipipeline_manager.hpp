#pragma once
#include "engine/core/config/config_types.hpp"
#include "engine/core/pipeline/pipeline_state.hpp"

namespace engine::core::pipeline {

/**
 * @brief Top-level pipeline lifecycle manager.
 * Used by app/ to initialize, start, stop, and query the pipeline.
 */
class IPipelineManager {
   public:
    virtual ~IPipelineManager() = default;
    virtual bool initialize(const engine::core::config::PipelineConfig& config) = 0;
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual bool pause() = 0;
    virtual bool resume() = 0;
    virtual PipelineState get_state() const = 0;
};

}  // namespace engine::core::pipeline
