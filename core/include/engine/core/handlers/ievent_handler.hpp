#pragma once
#include "engine/core/handlers/ihandler.hpp"
#include <any>
#include <cstdint>
#include <string>

namespace engine::core::handlers {

/**
 * @brief Context passed to event handlers when an event is dispatched.
 *
 * @details
 * - `event_type` matches an `engine::core::eventing` constant (e.g. ON_DETECT)
 * - `data` carries backend-specific metadata (NvDsBatchMeta*, GstBuffer*, etc.)
 *   The handler implementation (in pipeline/) casts to concrete types.
 */
struct HandlerContext {
    std::string event_type;    ///< Event type key (matches eventing constants)
    std::string element_id;    ///< Element that triggered this event
    int source_id{-1};         ///< Source/camera index (-1 = batch-level)
    uint64_t frame_number{0};  ///< Frame number (0 if not applicable)
    std::any data;             ///< Backend-specific payload
};

/**
 * @brief Extended handler interface with event dispatch.
 *
 * Concrete implementations live in `pipeline/event_handlers/` and
 * `pipeline/probes/`. Dependencies (IMessageProducer, IStorageManager)
 * are injected via their constructors — no static coupling.
 */
class IEventHandler : public IHandler {
   public:
    ~IEventHandler() override = default;

    /**
     * @brief Handle an incoming event.
     * @param ctx  Fully populated handler context.
     * @return true if the event was processed successfully.
     */
    virtual bool handle(const HandlerContext& ctx) = 0;
};

}  // namespace engine::core::handlers
