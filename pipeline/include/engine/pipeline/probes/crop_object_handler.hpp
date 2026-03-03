#pragma once

#include "engine/core/config/config_types.hpp"

#include <gst/gst.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::pipeline::probes {

/**
 * @brief Pad probe that crops detected objects from GPU frames.
 *
 * For each frame, iterates object metadata, checks label_filter,
 * and crops the bounding box region to save as JPEG images.
 * Respects capture_interval_sec to throttle I/O.
 */
class CropObjectHandler {
   public:
    CropObjectHandler() = default;
    ~CropObjectHandler() = default;

    void configure(const engine::core::config::EventHandlerConfig& config);

    static GstPadProbeReturn on_buffer(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);

   private:
    std::vector<std::string> label_filter_;
    std::string save_dir_;
    int capture_interval_sec_ = 5;
    int image_quality_ = 85;
    bool save_full_frame_ = true;

    /// Tracks last capture time per source_id to enforce interval
    std::unordered_map<int, int64_t> last_capture_time_;
};

}  // namespace engine::pipeline::probes
