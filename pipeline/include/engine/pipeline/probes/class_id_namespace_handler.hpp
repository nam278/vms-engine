#pragma once

#include "engine/core/config/config_types.hpp"

#include <gst/gst.h>
#include <string>
#include <unordered_map>

namespace engine::pipeline::probes {

/**
 * @brief Pad probe that namespaces class IDs based on GIE unique-id.
 *
 * When multiple inference engines operate (PGIE + SGIEs), each may
 * produce class IDs starting at 0. This probe remaps object class_id
 * to a globally unique namespace:
 *   global_id = gie_unique_id * 1000 + original_class_id
 *
 * Attach to the src pad of each nvinfer element.
 */
class ClassIdNamespaceHandler {
   public:
    ClassIdNamespaceHandler() = default;
    ~ClassIdNamespaceHandler() = default;

    /**
     * @brief Set the GIE unique ID for namespacing.
     * @param gie_unique_id  The gie-unique-id property of the nvinfer element.
     */
    void set_gie_unique_id(int gie_unique_id);

    static GstPadProbeReturn on_buffer(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);

   private:
    int gie_unique_id_ = 0;
};

}  // namespace engine::pipeline::probes
