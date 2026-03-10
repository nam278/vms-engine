#include "engine/pipeline/builders/source_builder.hpp"
#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

#include <sstream>

namespace engine::pipeline::builders {

SourceBuilder::SourceBuilder(GstElement* bin) : bin_(bin) {}

GstElement* SourceBuilder::build(const engine::core::config::PipelineConfig& config,
                                 int /*index*/) {
    const auto& src = config.sources;
    const std::string id = src.id.empty() ? std::string("sources") : src.id;

    auto elem = engine::core::utils::make_gst_element("nvmultiurisrcbin", id.c_str());
    if (!elem) {
        LOG_E("Failed to create nvmultiurisrcbin '{}'", id);
        return nullptr;
    }

    // Group 1 — nvmultiurisrcbin direct
    // NOTE: ip-address is intentionally NOT set — DS8's ip-address setter triggers SIGSEGV.
    //        The REST API server binds to 0.0.0.0 by default, which is acceptable.
    // "port" is a STRING property (per DS docs). 0 = disable CivetWeb REST API server.
    // rest_api_port=0 → disable; >0 → enable on that port (use e.g. 9000).
    const std::string rest_port_str = std::to_string(src.rest_api_port);
    g_object_set(G_OBJECT(elem.get()), "port", rest_port_str.c_str(), "max-batch-size",
                 static_cast<gint>(src.max_batch_size), "mode", static_cast<gint>(src.mode),
                 nullptr);
    if (src.rest_api_port > 0) {
        LOG_I("nvmultiurisrcbin REST API enabled on port {}", src.rest_api_port);
    } else {
        LOG_D("nvmultiurisrcbin REST API disabled (port=0)");
    }

    // Group 2 — nvurisrcbin per-source passthrough
    g_object_set(G_OBJECT(elem.get()), "gpu-id", static_cast<gint>(src.gpu_id),
                 "num-extra-surfaces", static_cast<gint>(src.num_extra_surfaces), "cudadec-memtype",
                 static_cast<gint>(src.cudadec_memtype), "dec-skip-frames",
                 static_cast<gint>(src.dec_skip_frames), "drop-frame-interval",
                 static_cast<gint>(src.drop_frame_interval), "select-rtp-protocol",
                 static_cast<gint>(src.select_rtp_protocol), "rtsp-reconnect-interval",
                 static_cast<gint>(src.rtsp_reconnect_interval), "rtsp-reconnect-attempts",
                 static_cast<gint>(src.rtsp_reconnect_attempts), "latency",
                 static_cast<guint>(src.latency), "udp-buffer-size",
                 static_cast<guint>(src.udp_buffer_size), "drop-pipeline-eos",
                 static_cast<gboolean>(src.drop_pipeline_eos), "disable-audio",
                 static_cast<gboolean>(src.disable_audio), "disable-passthrough",
                 static_cast<gboolean>(src.disable_passthrough), "file-loop",
                 static_cast<gboolean>(src.file_loop), "async-handling",
                 static_cast<gboolean>(src.async_handling), "low-latency-mode",
                 static_cast<gboolean>(src.low_latency_mode), nullptr);

    // Group 3 — nvstreammux passthrough
    g_object_set(G_OBJECT(elem.get()), "width", static_cast<gint>(src.width), "height",
                 static_cast<gint>(src.height), "batched-push-timeout",
                 static_cast<gint>(src.batched_push_timeout), "live-source",
                 static_cast<gboolean>(src.live_source), "sync-inputs",
                 static_cast<gboolean>(src.sync_inputs), nullptr);

    // init-rtsp-reconnect-interval: triggers reconnect on RTSP error (distinct from data-timeout).
    // Lantanav2 pattern: if not explicitly set (-1), fall back to rtsp_reconnect_interval.
    {
        const int irri = src.init_rtsp_reconnect_interval >= 0 ? src.init_rtsp_reconnect_interval
                                                               : src.rtsp_reconnect_interval;
        if (irri > 0) {
            g_object_set(G_OBJECT(elem.get()), "init-rtsp-reconnect-interval",
                         static_cast<gint>(irri), nullptr);
        }
    }

    // Smart Record properties
    if (src.smart_record > 0) {
        g_object_set(G_OBJECT(elem.get()), "smart-record", static_cast<gint>(src.smart_record),
                     "smart-rec-dir-path", src.smart_rec_dir_path.c_str(), "smart-rec-file-prefix",
                     src.smart_rec_file_prefix.c_str(), "smart-rec-cache",
                     static_cast<gint>(src.smart_rec_cache), "smart-rec-default-duration",
                     static_cast<gint>(src.smart_rec_default_duration), "smart-rec-mode",
                     static_cast<gint>(src.smart_rec_mode), "smart-rec-container",
                     static_cast<gint>(src.smart_rec_container), nullptr);
    }

    // uri-list: comma-separated URIs from cameras[] config.
    // Must be set before element reaches READY state (before gst_bin_add / state change).
    if (!src.cameras.empty()) {
        std::ostringstream oss;
        for (std::size_t i = 0; i < src.cameras.size(); ++i) {
            if (i > 0)
                oss << ',';
            oss << src.cameras[i].uri;
        }
        const std::string uri_list = oss.str();
        g_object_set(G_OBJECT(elem.get()), "uri-list", uri_list.c_str(), nullptr);
        LOG_D("nvmultiurisrcbin uri-list: {}", uri_list);
    }

    if (!gst_bin_add(GST_BIN(bin_), elem.get())) {
        LOG_E("Failed to add nvmultiurisrcbin '{}' to bin", id);
        return nullptr;
    }

    LOG_I("Built nvmultiurisrcbin '{}' (batch={}, {}x{}, {} cameras)", id, src.max_batch_size,
          src.width, src.height, src.cameras.size());
    return elem.release();
}

}  // namespace engine::pipeline::builders
