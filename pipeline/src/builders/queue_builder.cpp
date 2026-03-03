#include "engine/pipeline/builders/queue_builder.hpp"
#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::builders {

QueueBuilder::QueueBuilder(GstElement* bin) : bin_(bin) {}

GstElement* QueueBuilder::build(const engine::core::config::QueueConfig& cfg,
                                const std::string& name) {
    auto elem = engine::core::utils::make_gst_element("queue", name.c_str());
    if (!elem) {
        LOG_E("Failed to create queue '{}'", name);
        return nullptr;
    }

    // Convert max_size_bytes_mb to bytes (MB → bytes)
    guint max_size_bytes = static_cast<guint>(cfg.max_size_bytes_mb) * 1024 * 1024;

    // Convert max_size_time_sec to nanoseconds (sec → ns)
    guint64 max_size_time_ns = static_cast<guint64>(cfg.max_size_time_sec * 1e9);

    g_object_set(G_OBJECT(elem.get()), "max-size-buffers", static_cast<guint>(cfg.max_size_buffers),
                 "max-size-bytes", max_size_bytes, "max-size-time", max_size_time_ns, "leaky",
                 static_cast<gint>(cfg.leaky), "silent", static_cast<gboolean>(cfg.silent),
                 nullptr);

    if (!gst_bin_add(GST_BIN(bin_), elem.get())) {
        LOG_E("Failed to add queue '{}' to bin", name);
        return nullptr;
    }

    LOG_D("Built queue '{}' (bufs={}, leaky={})", name, cfg.max_size_buffers, cfg.leaky);
    return elem.release();
}

}  // namespace engine::pipeline::builders
