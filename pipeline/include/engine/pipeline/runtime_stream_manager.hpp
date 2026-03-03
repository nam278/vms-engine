#pragma once
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>
#include <string>
#include <vector>
#include <mutex>

namespace engine::pipeline {

/**
 * @brief Runtime stream management — add/remove camera URIs.
 *
 * Wraps the nvmultiurisrcbin REST API or direct GstBin manipulation
 * for runtime stream addition/removal without restarting the pipeline.
 */
class RuntimeStreamManager {
   public:
    explicit RuntimeStreamManager(GstElement* source_bin);

    /**
     * @brief Add a camera stream at runtime.
     * @param camera Camera config with name + URI.
     * @return true if the stream was added successfully.
     */
    bool add_stream(const engine::core::config::CameraConfig& camera);

    /**
     * @brief Remove a camera stream by id.
     * @param camera_id Unique camera id.
     * @return true if the stream was removed.
     */
    bool remove_stream(const std::string& camera_id);

    /**
     * @brief Get the current number of active streams.
     */
    int get_active_stream_count() const;

   private:
    GstElement* source_bin_ = nullptr;
    mutable std::mutex mtx_;
    std::vector<std::string> active_streams_;
};

}  // namespace engine::pipeline
