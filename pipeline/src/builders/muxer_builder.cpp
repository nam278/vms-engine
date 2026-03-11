#include "engine/pipeline/builders/muxer_builder.hpp"
#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::builders {

MuxerBuilder::MuxerBuilder(GstElement* bin) : bin_(bin) {}

GstElement* MuxerBuilder::build(const engine::core::config::PipelineConfig& config, int /*index*/) {
    const auto& src = config.sources;
    const auto& mux = src.mux;
    const std::string id = mux.id.empty() ? std::string("batch_mux") : mux.id;

    auto elem = engine::core::utils::make_gst_element("nvstreammux", id.c_str());
    if (!elem) {
        LOG_E("Failed to create nvstreammux '{}'", id);
        return nullptr;
    }

    // Manual mode opts into the newer nvstreammux implementation before gst_init().
    // Keep this builder on properties documented for the new mux path.
    g_object_set(G_OBJECT(elem.get()), "batch-size", static_cast<gint>(mux.batch_size),
                 "batched-push-timeout", static_cast<gint>(mux.batched_push_timeout_us),
                 "sync-inputs", static_cast<gboolean>(mux.sync_inputs), "max-latency",
                 static_cast<guint64>(mux.max_latency_ns), "drop-pipeline-eos",
                 static_cast<gboolean>(mux.drop_pipeline_eos), nullptr);

    if (mux.attach_sys_ts.has_value()) {
        g_object_set(G_OBJECT(elem.get()), "attach-sys-ts",
                     static_cast<gboolean>(*mux.attach_sys_ts), nullptr);
    }
    if (mux.frame_duration.has_value()) {
        if (*mux.frame_duration >= 0) {
            g_object_set(G_OBJECT(elem.get()), "frame-duration",
                         static_cast<guint64>(*mux.frame_duration), nullptr);
        } else {
            LOG_I("Skipping nvstreammux '{}' frame-duration override (value={})", id,
                  *mux.frame_duration);
        }
    }

    if (!mux.config_file_path.empty()) {
        LOG_W(
            "Applying nvstreammux '{}' config-file-path='{}'. External mux config can override "
            "live batching and pacing; verify overall-min/max-fps against real source FPS.",
            id, mux.config_file_path);
        g_object_set(G_OBJECT(elem.get()), "config-file-path", mux.config_file_path.c_str(),
                     nullptr);
    }

    if (!gst_bin_add(GST_BIN(bin_), elem.get())) {
        LOG_E("Failed to add nvstreammux '{}' to bin", id);
        return nullptr;
    }

    LOG_I("Built standalone nvstreammux '{}' (batch={}, sync_inputs={}, max_latency_ns={})", id,
          mux.batch_size, mux.sync_inputs, mux.max_latency_ns);
    return elem.release();
}

}  // namespace engine::pipeline::builders
