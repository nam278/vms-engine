#pragma once
#include "engine/core/handlers/ievent_handler.hpp"
#include "engine/core/config/config_types.hpp"
#include <string>
#include <vector>

namespace engine::pipeline::event_handlers {

/**
 * @brief Handles smart_record trigger events.
 *
 * When dispatched with ON_DETECT events matching label_filter,
 * starts NvDsSR recording on the relevant source_id.
 * Dependencies injected via constructor — no static coupling.
 */
class SmartRecordHandler : public engine::core::handlers::IEventHandler {
   public:
    SmartRecordHandler() = default;
    ~SmartRecordHandler() override = default;

    // IHandler
    bool initialize(const std::string& config_json) override;
    void destroy() override;
    std::string get_handler_name() const override;
    std::vector<std::string> get_supported_event_types() const override;

    // IEventHandler
    bool handle(const engine::core::handlers::HandlerContext& ctx) override;

   private:
    std::string id_;
    bool initialized_ = false;
};

}  // namespace engine::pipeline::event_handlers
