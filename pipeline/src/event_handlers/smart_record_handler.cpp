#include "engine/pipeline/event_handlers/smart_record_handler.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::event_handlers {

bool SmartRecordHandler::initialize(const std::string& config_json) {
    id_ = config_json;
    initialized_ = true;
    LOG_I("SmartRecordHandler '{}': initialized", id_);
    return true;
}

void SmartRecordHandler::destroy() {
    initialized_ = false;
    LOG_D("SmartRecordHandler '{}': destroyed", id_);
}

std::string SmartRecordHandler::get_handler_name() const {
    return "smart_record";
}

std::vector<std::string> SmartRecordHandler::get_supported_event_types() const {
    return {"on_detect", "smart_record"};
}

bool SmartRecordHandler::handle(const engine::core::handlers::HandlerContext& ctx) {
    if (!initialized_) {
        LOG_W("SmartRecordHandler: not initialized");
        return false;
    }

    LOG_D("SmartRecordHandler '{}': event='{}' source={} frame={}", id_, ctx.event_type,
          ctx.source_id, ctx.frame_number);

    // TODO: Integrate with SmartRecordController to call NvDsSRStart
    // 1. Check label_filter against detected objects in ctx.data
    // 2. If match, call smart_record_controller_->start_recording(ctx.source_id)
    // 3. Publish event to broker if configured

    return true;
}

}  // namespace engine::pipeline::event_handlers
