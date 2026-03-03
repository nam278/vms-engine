#include "engine/pipeline/smart_record_controller.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline {

bool SmartRecordController::initialize(GstElement* source_bin,
                                       const engine::core::config::SourcesConfig& config) {
    source_bin_ = source_bin;
    pre_event_sec_ = config.smart_rec_cache;
    post_event_sec_ = config.smart_rec_default_duration;

    if (config.smart_record == 0) {
        LOG_I("Smart record disabled");
        return true;
    }

    LOG_I("SmartRecordController initialized (pre={}s, post={}s, dir='{}')", pre_event_sec_,
          post_event_sec_, config.smart_rec_dir_path);
    return true;
}

bool SmartRecordController::start_recording(int source_id) {
    std::lock_guard<std::mutex> lock(mtx_);

    if (recording_state_[source_id]) {
        LOG_D("Source {} already recording", source_id);
        return true;
    }

    // TODO: Call NvDsSRStart() via nvmultiurisrcbin "sr-done" callback
    // or emit GSignal for smart record start.
    LOG_I("Smart record START for source {}", source_id);
    recording_state_[source_id] = true;
    return true;
}

bool SmartRecordController::stop_recording(int source_id) {
    std::lock_guard<std::mutex> lock(mtx_);

    if (!recording_state_[source_id]) {
        LOG_D("Source {} not recording", source_id);
        return true;
    }

    // TODO: Call NvDsSRStop()
    LOG_I("Smart record STOP for source {}", source_id);
    recording_state_[source_id] = false;
    return true;
}

bool SmartRecordController::is_recording(int source_id) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = recording_state_.find(source_id);
    return it != recording_state_.end() && it->second;
}

}  // namespace engine::pipeline
