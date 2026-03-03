#pragma once

#include "engine/core/config/config_types.hpp"

#include <gst/gst.h>
#include <string>
#include <vector>

namespace engine::pipeline::probes {

/**
 * @brief Manages GStreamer pad probes based on EventHandlerConfig entries.
 *
 * For each enabled config entry, creates an appropriately typed probe handler
 * (SmartRecordProbeHandler or CropObjectHandler), configures it, and attaches
 * it as a GST_PAD_PROBE_TYPE_BUFFER probe to the configured probe_element's
 * src pad.
 *
 * Ownership of each probe handler instance is transferred to GStreamer via the
 * GDestroyNotify callback — no additional lifecycle management is needed.
 */
class ProbeHandlerManager {
   public:
    /**
     * @param pipeline  GstBin containing all elements for pad lookup.
     */
    explicit ProbeHandlerManager(GstElement* pipeline);

    ~ProbeHandlerManager() = default;

    /**
     * @brief Attach probes based on EventHandlerConfig entries from YAML.
     * @return true if all enabled probes were attached successfully.
     */
    bool attach_probes(const std::vector<engine::core::config::EventHandlerConfig>& configs);

    /**
     * @brief Remove all attached probes (call before pipeline teardown).
     */
    void detach_all();

   private:
    GstElement* pipeline_;

    struct ProbeEntry {
        GstElement* element;
        GstPad* pad;
        gulong probe_id;
        std::string handler_id;
    };
    std::vector<ProbeEntry> probes_;

    /// Finds element by name in the pipeline bin.
    GstElement* find_element(const std::string& name) const;
};

}  // namespace engine::pipeline::probes
