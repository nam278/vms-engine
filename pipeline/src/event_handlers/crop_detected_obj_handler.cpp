#include "engine/pipeline/event_handlers/crop_detected_obj_handler.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::event_handlers {

bool CropDetectedObjHandler::initialize(const std::string& config_json) {
    id_ = config_json;
    initialized_ = true;
    LOG_I("CropDetectedObjHandler '{}': initialized", id_);
    return true;
}

void CropDetectedObjHandler::destroy() {
    initialized_ = false;
    LOG_D("CropDetectedObjHandler '{}': destroyed", id_);
}

std::string CropDetectedObjHandler::get_handler_name() const {
    return "crop_detected_obj";
}

std::vector<std::string> CropDetectedObjHandler::get_supported_event_types() const {
    return {"on_detect", "crop_objects"};
}

bool CropDetectedObjHandler::handle(const engine::core::handlers::HandlerContext& ctx) {
    if (!initialized_) {
        LOG_W("CropDetectedObjHandler: not initialized");
        return false;
    }

    LOG_D("CropDetectedObjHandler '{}': event='{}' source={} frame={}", id_, ctx.event_type,
          ctx.source_id, ctx.frame_number);

    // TODO: Implement object cropping
    // 1. Extract NvBufSurface* and NvDsObjectMeta list from ctx.data
    // 2. For each detected object matching label_filter:
    //    a. Map NvBufSurfaceMap() to access GPU surface
    //    b. Crop bounding box region
    //    c. Encode to JPEG with image_quality
    //    d. Save to save_dir/source_{source_id}/frame_{frame_number}_{obj_id}.jpg
    // 3. Unmap surface

    return true;
}

}  // namespace engine::pipeline::event_handlers
