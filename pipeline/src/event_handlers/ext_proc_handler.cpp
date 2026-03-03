#include "engine/pipeline/event_handlers/ext_proc_handler.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::event_handlers {

bool ExtProcHandler::initialize(const std::string& config_json) {
    id_ = config_json;
    initialized_ = true;
    LOG_I("ExtProcHandler '{}': initialized", id_);
    return true;
}

void ExtProcHandler::destroy() {
    initialized_ = false;
    LOG_D("ExtProcHandler '{}': destroyed", id_);
}

std::string ExtProcHandler::get_handler_name() const {
    return "ext_proc";
}

std::vector<std::string> ExtProcHandler::get_supported_event_types() const {
    return {"on_detect", "ext_proc"};
}

bool ExtProcHandler::handle(const engine::core::handlers::HandlerContext& ctx) {
    if (!initialized_) {
        LOG_W("ExtProcHandler: not initialized");
        return false;
    }

    LOG_D("ExtProcHandler '{}': event='{}' source={} frame={}", id_, ctx.event_type, ctx.source_id,
          ctx.frame_number);

    // TODO: Forward event to external processor
    // 1. Serialize ctx to JSON (event_type, element_id, source_id, frame_number)
    // 2. If broker configured, publish via IMessageProducer
    // 3. If HTTP endpoint configured, send POST request via libcurl
    // 4. Return success/failure based on delivery acknowledgment

    return true;
}

}  // namespace engine::pipeline::event_handlers
