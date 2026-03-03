#pragma once

#include <gst/gst.h>
#include <string>

namespace engine::pipeline::linking {

/**
 * @brief Static and dynamic linking helpers for GStreamer pipeline elements.
 *
 * - link()             — links src→sink via fixed pads (most elements)
 * - link_filtered()    — links with caps filter
 * - connect_dynamic()  — connects pad-added signal for dynamic sources
 *                        (nvmultiurisrcbin → muxer)
 */
class PipelineLinker {
   public:
    /**
     * @brief Statically link two elements via their default src/sink pads.
     * @return true on success, false with LOG_E on failure.
     */
    static bool link(GstElement* src, GstElement* sink);

    /**
     * @brief Statically link two elements with a caps filter.
     * @param caps_str  GStreamer caps string (e.g. "video/x-raw(memory:NVMM)")
     */
    static bool link_filtered(GstElement* src, GstElement* sink, const std::string& caps_str);

    /**
     * @brief Register dynamic pad-added callback for nvmultiurisrcbin → muxer.
     *
     * When nvmultiurisrcbin creates a new src pad (per camera stream),
     * the callback requests a sink_%u pad on the muxer and links them.
     */
    static void connect_dynamic(GstElement* source_bin, GstElement* muxer);

   private:
    /// GSignal callback for "pad-added" from nvmultiurisrcbin
    static void on_pad_added(GstElement* src, GstPad* new_pad, GstElement* muxer);
};

}  // namespace engine::pipeline::linking
