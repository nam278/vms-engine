#pragma once
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>
#include <string>
#include <unordered_map>
#include <mutex>

namespace engine::pipeline {

/**
 * @brief Wraps the NvDsSR (Smart Record) API.
 *
 * Manages start/stop of smart recording sessions per source.
 * Called by SmartRecordHandler when detection criteria are met.
 */
class SmartRecordController {
   public:
    SmartRecordController() = default;

    /**
     * @brief Initialize with source bin and config.
     * @param source_bin The nvmultiurisrcbin element.
     * @param config     Sources config containing smart_record fields.
     * @return true if smart record context initialized.
     */
    bool initialize(GstElement* source_bin, const engine::core::config::SourcesConfig& config);

    /**
     * @brief Start recording for a given source.
     * @param source_id Source/camera index.
     * @return true if recording started.
     */
    bool start_recording(int source_id);

    /**
     * @brief Stop recording for a given source.
     * @param source_id Source/camera index.
     * @return true if recording stopped.
     */
    bool stop_recording(int source_id);

    /**
     * @brief Check if a source is currently recording.
     */
    bool is_recording(int source_id) const;

   private:
    GstElement* source_bin_ = nullptr;
    mutable std::mutex mtx_;
    std::unordered_map<int, bool> recording_state_;
    int pre_event_sec_ = 10;
    int post_event_sec_ = 20;
};

}  // namespace engine::pipeline
