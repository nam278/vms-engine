#pragma once
#include "engine/core/handlers/ievent_handler.hpp"
#include <string>
#include <vector>

namespace engine::pipeline::event_handlers {

/**
 * @brief Crops detected objects from frames and saves as image files.
 *
 * Triggered for ON_DETECT events. Extracts NvDsObjectMeta bounding boxes
 * from ctx.data, crops corresponding regions from the GPU buffer,
 * and writes JPEG images to save_dir.
 */
class CropDetectedObjHandler : public engine::core::handlers::IEventHandler {
   public:
    CropDetectedObjHandler() = default;
    ~CropDetectedObjHandler() override = default;

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
