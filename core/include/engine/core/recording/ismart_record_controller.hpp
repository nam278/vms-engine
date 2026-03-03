#pragma once
#include <gst/gst.h>

namespace engine::core::recording {

/**
 * @brief Controls NvDsSR (Smart Record) on nvmultiurisrcbin.
 */
class ISmartRecordController {
   public:
    virtual ~ISmartRecordController() = default;
    virtual bool start_recording(int source_id, int duration_sec = 0) = 0;
    virtual bool stop_recording(int source_id) = 0;
};

}  // namespace engine::core::recording
