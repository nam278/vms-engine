#pragma once
#include "engine/core/config/config_types.hpp"
#include "engine/core/pipeline/runtime_source_control_types.hpp"
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

    virtual RuntimeSourceMutationResult list_sources_detailed() = 0;
    virtual RuntimeSourceMutationResult add_source_detailed(
        const engine::core::config::CameraConfig& camera) = 0;
    virtual RuntimeSourceMutationResult remove_source_detailed(const std::string& camera_id) = 0;

    virtual std::vector<RuntimeSourceInfo> list_sources() {
        return list_sources_detailed().sources;
    }

    virtual bool add_source(const engine::core::config::CameraConfig& camera) {
        return add_source_detailed(camera).success;
    }

    virtual bool remove_source(const std::string& camera_id) {
        return remove_source_detailed(camera_id).success;
    }

    virtual PipelineState get_state() const = 0;
};

}  // namespace engine::core::pipeline
