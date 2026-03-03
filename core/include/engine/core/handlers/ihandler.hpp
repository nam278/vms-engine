#pragma once
#include <string>
#include <vector>

namespace engine::core::handlers {

/**
 * @brief Base interface for event handlers (pad probes, signal handlers).
 * Concrete handlers in pipeline/ receive dependencies via constructor injection.
 * NO static members, NO infrastructure types.
 */
class IHandler {
   public:
    virtual ~IHandler() = default;
    virtual bool initialize(const std::string& config_json) = 0;
    virtual void destroy() = 0;
    virtual std::string get_handler_name() const = 0;
    virtual std::vector<std::string> get_supported_event_types() const = 0;
};

}  // namespace engine::core::handlers
