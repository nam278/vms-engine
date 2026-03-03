#pragma once
#include "engine/core/config/config_types.hpp"
#include <gst/gst.h>
#include <string>

namespace engine::pipeline::builders {

/**
 * @brief Builds GStreamer queue element with QueueConfig settings.
 *
 * Unlike other builders, QueueBuilder does NOT implement IElementBuilder.
 * It takes QueueConfig directly (already resolved by BlockBuilder from config).
 */
class QueueBuilder {
   public:
    explicit QueueBuilder(GstElement* bin);

    /**
     * @brief Create and configure a queue element.
     * @param cfg   Resolved queue config (max_size_buffers, leaky, etc.)
     * @param name  Element name (unique within the pipeline).
     * @return Configured GstElement* (bin owns), or nullptr on failure.
     */
    GstElement* build(const engine::core::config::QueueConfig& cfg, const std::string& name);

   private:
    GstElement* bin_ = nullptr;
};

}  // namespace engine::pipeline::builders
