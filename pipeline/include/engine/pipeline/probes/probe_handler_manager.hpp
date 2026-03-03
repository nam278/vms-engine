#pragma once

#include "engine/core/config/config_types.hpp"
#include "engine/core/handlers/ihandler_manager.hpp"

#include <gst/gst.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::pipeline::probes {

/**
 * @brief Manages GStreamer pad probes and routes buffer events to handlers.
 *
 * Reads EventHandlerConfig entries, attaches pad probes to the configured
 * probe_element, and dispatches HandlerContext to IHandlerManager on each
 * buffer passing through the probe.
 */
class ProbeHandlerManager {
   public:
    /**
     * @param handler_manager  Shared handler manager for dispatch.
     * @param pipeline         GstBin containing all elements for pad lookup.
     */
    explicit ProbeHandlerManager(
        std::shared_ptr<engine::core::handlers::IHandlerManager> handler_manager,
        GstElement* pipeline);

    ~ProbeHandlerManager() = default;

    /**
     * @brief Attach probes based on event_handler configs.
     * @return true if all requested probes were attached successfully.
     */
    bool attach_probes(const std::vector<engine::core::config::EventHandlerConfig>& configs);

    /**
     * @brief Remove all attached probes (call before pipeline teardown).
     */
    void detach_all();

   private:
    std::shared_ptr<engine::core::handlers::IHandlerManager> handler_manager_;
    GstElement* pipeline_;

    struct ProbeEntry {
        GstElement* element;
        GstPad* pad;
        gulong probe_id;
        std::string handler_id;
    };
    std::vector<ProbeEntry> probes_;

    /// Finds element by name in the pipeline bin
    GstElement* find_element(const std::string& name) const;
};

}  // namespace engine::pipeline::probes
