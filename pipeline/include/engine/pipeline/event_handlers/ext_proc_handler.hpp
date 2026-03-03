#pragma once
#include "engine/core/handlers/ievent_handler.hpp"
#include <string>
#include <vector>

namespace engine::pipeline::event_handlers {

/**
 * @brief Forwards event data to an external processor via messaging or HTTP.
 *
 * Used for custom integrations where event processing happens outside the
 * engine (e.g., sending detection metadata to a Python service for
 * secondary classification, or forwarding to a Redis stream).
 */
class ExtProcHandler : public engine::core::handlers::IEventHandler {
   public:
    ExtProcHandler() = default;
    ~ExtProcHandler() override = default;

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
