#pragma once
#include "engine/core/config/config_types.hpp"
#include "engine/core/pipeline/runtime_source_control_types.hpp"

#include <gst/gst.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::pipeline {

/**
 * @brief Runtime stream management for manual nvurisrcbin + nvstreammux graphs.
 *
 * The manager creates one nvurisrcbin per camera and requests a matching
 * nvstreammux sink pad so DeepStream keeps batched inference active while
 * cameras are added or removed at runtime.
 */
class RuntimeStreamManager {
   public:
    RuntimeStreamManager(GstElement* source_root, GstElement* muxer,
                         engine::core::config::SourcesConfig sources_config);

    ~RuntimeStreamManager();

    /**
     * @brief Add a camera stream at runtime.
     * @param camera Camera config with name + URI.
     * @return true if the stream was added successfully.
     */
    bool add_stream(const engine::core::config::CameraConfig& camera);

    engine::core::pipeline::RuntimeSourceMutationResult add_stream_detailed(
        const engine::core::config::CameraConfig& camera);

    /**
     * @brief Remove a camera stream by id.
     * @param camera_id Unique camera id.
     * @return true if the stream was removed.
     */
    bool remove_stream(const std::string& camera_id);

    engine::core::pipeline::RuntimeSourceMutationResult remove_stream_detailed(
        const std::string& camera_id);

    std::vector<engine::core::pipeline::RuntimeSourceInfo> list_streams() const;

    /**
     * @brief Get the current number of active streams.
     */
    int get_active_stream_count() const;

   private:
    struct StreamSlot {
        uint32_t source_index = 0;
        std::string camera_id;
        std::string camera_uri;
        bool is_seeded = false;
        std::string state = "active";
        GstElement* source = nullptr;
        GstElement* pad_signal_source = nullptr;
        GstPad* mux_sink_pad = nullptr;
        gulong pad_added_handler_id = 0;
    };

    GstElement* source_root_ = nullptr;
    GstElement* muxer_ = nullptr;
    engine::core::config::SourcesConfig sources_config_;
    mutable std::mutex mtx_;
    std::unordered_map<std::string, StreamSlot> streams_;
    std::vector<uint32_t> free_source_indexes_;
    uint32_t next_source_index_ = 0;

    uint32_t allocate_source_index();
    void release_source_index(uint32_t source_index);
    void seed_existing_streams();
};

}  // namespace engine::pipeline
