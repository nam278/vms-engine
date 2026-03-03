#include "engine/pipeline/builders/muxer_builder.hpp"
#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::builders {

MuxerBuilder::MuxerBuilder(GstElement* bin) : bin_(bin) {}

GstElement* MuxerBuilder::build(const engine::core::config::PipelineConfig& config, int /*index*/) {
    const auto& src = config.sources;
    const std::string id = "nvstreammux0";

    auto elem = engine::core::utils::make_gst_element("nvstreammux", id.c_str());
    if (!elem) {
        LOG_E("Failed to create nvstreammux '{}'", id);
        return nullptr;
    }

    g_object_set(G_OBJECT(elem.get()), "batch-size", static_cast<gint>(src.max_batch_size), "width",
                 static_cast<gint>(src.width), "height", static_cast<gint>(src.height),
                 "batched-push-timeout", static_cast<gint>(src.batched_push_timeout), "live-source",
                 static_cast<gboolean>(src.live_source), "gpu-id", static_cast<gint>(src.gpu_id),
                 nullptr);

    if (!gst_bin_add(GST_BIN(bin_), elem.get())) {
        LOG_E("Failed to add nvstreammux '{}' to bin", id);
        return nullptr;
    }

    LOG_I("Built nvstreammux '{}' (batch={}, {}x{})", id, src.max_batch_size, src.width,
          src.height);
    return elem.release();
}

}  // namespace engine::pipeline::builders
