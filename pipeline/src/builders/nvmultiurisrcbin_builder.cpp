#include "engine/pipeline/builders/nvmultiurisrcbin_builder.hpp"

#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

#include <sstream>

namespace engine::pipeline::builders {

NvMultiUriSrcBinBuilder::NvMultiUriSrcBinBuilder(GstElement* bin) : bin_(bin) {}

GstElement* NvMultiUriSrcBinBuilder::build(const engine::core::config::PipelineConfig& config,
                                           int /*index*/) {
    const auto& src = config.sources;
    const std::string id = src.id.empty() ? std::string("sources") : src.id;
    const std::string smart_record_prefix =
        config.pipeline.id.empty() ? src.smart_rec_file_prefix : config.pipeline.id;

    auto elem = engine::core::utils::make_gst_element("nvmultiurisrcbin", id.c_str());
    if (!elem) {
        LOG_E("Failed to create nvmultiurisrcbin '{}'", id);
        return nullptr;
    }

    // DS8 ip-address setter is unstable in this repo; keep DeepStream's default bind behavior.
    const std::string rest_port_str = std::to_string(src.rest_api_port);
    g_object_set(G_OBJECT(elem.get()), "port", rest_port_str.c_str(), "max-batch-size",
                 static_cast<gint>(src.max_batch_size), "mode", static_cast<gint>(src.mode),
                 nullptr);

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

    g_object_set(G_OBJECT(elem.get()), "width", static_cast<gint>(src.width), "height",
                 static_cast<gint>(src.height), "batched-push-timeout",
                 static_cast<gint>(src.batched_push_timeout), "live-source",
                 static_cast<gboolean>(src.live_source), "sync-inputs",
                 static_cast<gboolean>(src.sync_inputs), nullptr);

    const int init_reconnect_interval = src.init_rtsp_reconnect_interval >= 0
                                            ? src.init_rtsp_reconnect_interval
                                            : src.rtsp_reconnect_interval;
    if (init_reconnect_interval > 0) {
        g_object_set(G_OBJECT(elem.get()), "init-rtsp-reconnect-interval",
                     static_cast<gint>(init_reconnect_interval), nullptr);
    }

    if (src.smart_record > 0) {
        g_object_set(G_OBJECT(elem.get()), "smart-record", static_cast<gint>(src.smart_record),
                     "smart-rec-dir-path", src.smart_rec_dir_path.c_str(), "smart-rec-file-prefix",
                     smart_record_prefix.c_str(), "smart-rec-cache",
                     static_cast<gint>(src.smart_rec_cache), "smart-rec-default-duration",
                     static_cast<gint>(src.smart_rec_default_duration), "smart-rec-mode",
                     static_cast<gint>(src.smart_rec_mode), "smart-rec-container",
                     static_cast<gint>(src.smart_rec_container), nullptr);
    }

    if (!src.cameras.empty()) {
        std::ostringstream uri_stream;
        std::ostringstream sensor_id_stream;
        std::ostringstream sensor_name_stream;
        for (std::size_t i = 0; i < src.cameras.size(); ++i) {
            if (i > 0) {
                uri_stream << ',';
                sensor_id_stream << ',';
                sensor_name_stream << ',';
            }

            uri_stream << src.cameras[i].uri;
            const std::string sensor_id = src.cameras[i].id.empty()
                                              ? std::string("camera-") + std::to_string(i)
                                              : src.cameras[i].id;
            sensor_id_stream << sensor_id;
            sensor_name_stream << sensor_id;
        }

        const std::string uri_list = uri_stream.str();
        const std::string sensor_id_list = sensor_id_stream.str();
        const std::string sensor_name_list = sensor_name_stream.str();
        g_object_set(G_OBJECT(elem.get()), "uri-list", uri_list.c_str(), "sensor-id-list",
                     sensor_id_list.c_str(), "sensor-name-list", sensor_name_list.c_str(), nullptr);
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