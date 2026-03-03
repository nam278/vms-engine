#pragma once
#include "engine/core/handlers/ievent_handler.hpp"
#include "engine/core/config/config_types.hpp"
#include <memory>
#include <string>
#include <vector>

namespace engine::core::handlers {

/**
 * @brief Manages the lifecycle of event handlers (registration, dispatch).
 *
 * Implemented by `pipeline/event_handlers/handler_manager.hpp`.
 * Initialized with EventHandlerConfig entries from the parsed YAML.
 */
class IHandlerManager {
   public:
    virtual ~IHandlerManager() = default;

    /**
     * @brief Register a handler instance.
     * @param handler Shared pointer to an initialized event handler.
     * @return true if registration succeeded.
     */
    virtual bool register_handler(std::shared_ptr<IEventHandler> handler) = 0;

    /**
     * @brief Initialize all handlers from config entries.
     * @param configs Event handler config list from PipelineConfig.
     * @return true if all enabled handlers initialized successfully.
     */
    virtual bool initialize_handlers(
        const std::vector<engine::core::config::EventHandlerConfig>& configs) = 0;

    /**
     * @brief Dispatch an event to all registered handlers that support it.
     * @param ctx Handler context with event type + payload.
     */
    virtual void dispatch(const HandlerContext& ctx) = 0;

    /**
     * @brief Destroy and unregister all handlers.
     */
    virtual void destroy_all() = 0;
};

}  // namespace engine::core::handlers
