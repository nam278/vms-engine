#include "engine/pipeline/runtime_stream_manager.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline {

RuntimeStreamManager::RuntimeStreamManager(GstElement* source_bin) : source_bin_(source_bin) {}

bool RuntimeStreamManager::add_stream(const engine::core::config::CameraConfig& camera) {
    std::lock_guard<std::mutex> lock(mtx_);

    if (!source_bin_) {
        LOG_E("Source bin is null — cannot add stream '{}'", camera.id);
        return false;
    }

    // nvmultiurisrcbin uses REST API (POST /add) or sensor-id-list property.
    // For runtime add, emit "add-source" signal if supported by the element.
    // This is a stub — actual implementation depends on DeepStream version.
    LOG_I("Adding stream '{}' (uri={})", camera.id, camera.uri);

    // TODO: Implement via nvmultiurisrcbin REST API or GSignal
    active_streams_.push_back(camera.id);
    return true;
}

bool RuntimeStreamManager::remove_stream(const std::string& camera_id) {
    std::lock_guard<std::mutex> lock(mtx_);

    if (!source_bin_) {
        LOG_E("Source bin is null — cannot remove stream '{}'", camera_id);
        return false;
    }

    auto it = std::find(active_streams_.begin(), active_streams_.end(), camera_id);
    if (it == active_streams_.end()) {
        LOG_W("Stream '{}' not found in active list", camera_id);
        return false;
    }

    LOG_I("Removing stream '{}'", camera_id);
    // TODO: Implement via nvmultiurisrcbin REST API or GSignal
    active_streams_.erase(it);
    return true;
}

int RuntimeStreamManager::get_active_stream_count() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return static_cast<int>(active_streams_.size());
}

}  // namespace engine::pipeline
