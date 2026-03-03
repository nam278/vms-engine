#pragma once

#include "engine/core/config/config_types.hpp"

#include <gst/gst.h>
#include <string>
#include <vector>

namespace engine::pipeline::probes {

/**
 * @brief Pad probe handler that triggers smart recording on object detection.
 *
 * Attaches to the configured probe element's src pad, iterates NvDs metadata,
 * checks label_filter, and calls NvDsSRStart() on the source bin
 * when matching objects are found.
 */
class SmartRecordProbeHandler {
   public:
    SmartRecordProbeHandler() = default;
    ~SmartRecordProbeHandler() = default;

    /**
     * @brief Configure label filter and recording parameters.
     * @param config  EventHandlerConfig with trigger=smart_record.
     */
    void configure(const engine::core::config::EventHandlerConfig& config);

    /**
     * @brief The static probe callback registered via gst_pad_add_probe.
     *
     * user_data must point to a SmartRecordProbeHandler instance.
     */
    static GstPadProbeReturn on_buffer(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);

   private:
    std::vector<std::string> label_filter_;
    int pre_event_sec_ = 2;
    int post_event_sec_ = 20;
    std::string save_dir_;
};

}  // namespace engine::pipeline::probes
