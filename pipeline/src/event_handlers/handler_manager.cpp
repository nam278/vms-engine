#include "engine/pipeline/event_handlers/handler_manager.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::event_handlers {

bool HandlerManager::register_handler(
    std::shared_ptr<engine::core::handlers::IEventHandler> handler) {
    if (!handler) {
        LOG_E("HandlerManager: cannot register null handler");
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    handlers_.push_back(std::move(handler));
    rebuild_type_map();

    LOG_D("HandlerManager: registered handler '{}' (total={})",
          handlers_.back()->get_handler_name(), handlers_.size());
    return true;
}

bool HandlerManager::initialize_handlers(
    const std::vector<engine::core::config::EventHandlerConfig>& configs) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& cfg : configs) {
        if (!cfg.enable) {
            LOG_D("HandlerManager: handler '{}' disabled, skipping", cfg.id);
            continue;
        }

        // Find a registered handler that matches the trigger
        bool found = false;
        for (auto& handler : handlers_) {
            const auto& supported = handler->get_supported_event_types();
            for (const auto& st : supported) {
                if (st == cfg.trigger || st == cfg.type) {
                    // Initialize with config JSON (simplified — pass ID)
                    if (!handler->initialize(cfg.id)) {
                        LOG_E("HandlerManager: failed to initialize '{}'", cfg.id);
                        return false;
                    }
                    found = true;
                    break;
                }
            }
            if (found)
                break;
        }

        if (!found) {
            LOG_W("HandlerManager: no handler found for trigger='{}' (id='{}')", cfg.trigger,
                  cfg.id);
        }
    }

    LOG_I("HandlerManager: initialized {} handlers from {} config entries", handlers_.size(),
          configs.size());
    return true;
}

void HandlerManager::dispatch(const engine::core::handlers::HandlerContext& ctx) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = type_map_.find(ctx.event_type);
    if (it == type_map_.end()) {
        return;  // No handlers for this event type
    }

    for (size_t idx : it->second) {
        if (idx < handlers_.size()) {
            if (!handlers_[idx]->handle(ctx)) {
                LOG_W("HandlerManager: handler '{}' failed for event '{}'",
                      handlers_[idx]->get_handler_name(), ctx.event_type);
            }
        }
    }
}

void HandlerManager::destroy_all() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& handler : handlers_) {
        handler->destroy();
        LOG_D("HandlerManager: destroyed handler '{}'", handler->get_handler_name());
    }
    handlers_.clear();
    type_map_.clear();

    LOG_I("HandlerManager: all handlers destroyed");
}

void HandlerManager::rebuild_type_map() {
    type_map_.clear();
    for (size_t i = 0; i < handlers_.size(); ++i) {
        for (const auto& event_type : handlers_[i]->get_supported_event_types()) {
            type_map_[event_type].push_back(i);
        }
    }
}

}  // namespace engine::pipeline::event_handlers
