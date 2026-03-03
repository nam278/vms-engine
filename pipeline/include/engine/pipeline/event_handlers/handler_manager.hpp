#pragma once
#include "engine/core/handlers/ihandler_manager.hpp"
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::pipeline::event_handlers {

/**
 * @brief Concrete IHandlerManager — registers, initializes, and dispatches
 *        event handlers based on EventHandlerConfig from YAML.
 *
 * Thread-safe: dispatch() may be called from GStreamer streaming threads
 * (pad probes, signal callbacks).
 */
class HandlerManager : public engine::core::handlers::IHandlerManager {
   public:
    HandlerManager() = default;
    ~HandlerManager() override = default;

    bool register_handler(std::shared_ptr<engine::core::handlers::IEventHandler> handler) override;

    bool initialize_handlers(
        const std::vector<engine::core::config::EventHandlerConfig>& configs) override;

    void dispatch(const engine::core::handlers::HandlerContext& ctx) override;

    void destroy_all() override;

   private:
    mutable std::mutex mutex_;

    /// All registered handlers
    std::vector<std::shared_ptr<engine::core::handlers::IEventHandler>> handlers_;

    /// event_type → [handler indices] for O(1) dispatch lookup
    std::unordered_map<std::string, std::vector<size_t>> type_map_;

    void rebuild_type_map();
};

}  // namespace engine::pipeline::event_handlers
