#pragma once

#include "engine/core/config/config_types.hpp"

#include <gst/gst.h>
#include <string>
#include <vector>

namespace engine::core::messaging {
class IMessageProducer;
}

namespace engine::pipeline::evidence {
class FrameEvidenceCache;
}

namespace engine::pipeline::extproc {
class FrameEventsExtProcService;
}

namespace engine::pipeline::probes {

/**
 * @brief Manages GStreamer pad probes for the entire pipeline.
 *
 * Creates and attaches probe handlers based on `event_handlers[]` in the
 * PipelineConfig.  Supported triggers:
 *   - `"smart_record"` → SmartRecordProbeHandler
 *   - `"crop_objects"` → CropObjectHandler
 *   - `"class_id_offset"` → ClassIdNamespaceHandler (Offset mode)
 *   - `"class_id_restore"` → ClassIdNamespaceHandler (Restore mode)
 *   - `"frame_events"` → FrameEventsProbeHandler
 *
 * Ownership of each handler is transferred to GStreamer via GDestroyNotify;
 * no additional lifecycle management is required by the caller.
 */
class ProbeHandlerManager {
   public:
    /**
     * @param pipeline  Top-level GstBin containing all named elements.
     */
    explicit ProbeHandlerManager(GstElement* pipeline);

    ~ProbeHandlerManager() = default;

    /**
     * @brief Attach all enabled probes defined in the pipeline config.
     *
     * @param config   Full immutable pipeline configuration.
     * @param producer Message producer for handlers that publish events
     *                 (SmartRecord, CropObjects). May be nullptr if no
     *                 messaging is configured.
     * @return true if every enabled probe was attached successfully.
     */
    bool attach_probes(const engine::core::config::PipelineConfig& config,
                       engine::core::messaging::IMessageProducer* producer,
                       engine::pipeline::evidence::FrameEvidenceCache* cache,
                       engine::pipeline::extproc::FrameEventsExtProcService* ext_proc_service);

    /**
     * @brief Remove all attached probes (call before pipeline teardown).
     */
    void detach_all();

   private:
    GstElement* pipeline_;

    struct ProbeEntry {
        GstElement* element;     ///< Element the probe is on (not owned)
        GstPad* pad;             ///< Pad the probe is on (owned ref, must unref)
        gulong probe_id;         ///< GStreamer probe identifier
        std::string handler_id;  ///< Config id for logging
    };
    std::vector<ProbeEntry> probes_;

    /// Find a named element inside the pipeline bin.
    GstElement* find_element(const std::string& name) const;

    /// Find the processing element index by config id.
    int find_processing_element_index(const engine::core::config::PipelineConfig& config,
                                      const std::string& element_id) const;
};

}  // namespace engine::pipeline::probes
