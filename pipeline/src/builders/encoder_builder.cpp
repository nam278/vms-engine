#include "engine/pipeline/builders/encoder_builder.hpp"
#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::builders {

EncoderBuilder::EncoderBuilder(GstElement* bin) : bin_(bin) {}

GstElement* EncoderBuilder::build(const engine::core::config::PipelineConfig& config, int index) {
    // Decode compound index: output_index * 100 + element_index
    int output_idx = index / 100;
    int element_idx = index % 100;

    const auto& elem_cfg = config.outputs[output_idx].elements[element_idx];
    const auto& id = elem_cfg.id;
    const auto& type = elem_cfg.type;  // "nvv4l2h264enc" or "nvv4l2h265enc"

    auto elem = engine::core::utils::make_gst_element(type.c_str(), id.c_str());
    if (!elem) {
        LOG_E("Failed to create encoder '{}' (type={})", id, type);
        return nullptr;
    }

    g_object_set(G_OBJECT(elem.get()), "gpu-id", static_cast<guint>(elem_cfg.gpu_id), nullptr);

    // Bitrate
    if (elem_cfg.bitrate > 0) {
        g_object_set(G_OBJECT(elem.get()), "bitrate", static_cast<guint>(elem_cfg.bitrate),
                     nullptr);
    }

    // I-frame interval
    if (elem_cfg.iframeinterval > 0) {
        g_object_set(G_OBJECT(elem.get()), "iframeinterval",
                     static_cast<guint>(elem_cfg.iframeinterval), nullptr);
    }

    if (!elem_cfg.control_rate.empty()) {
        gst_util_set_object_arg(G_OBJECT(elem.get()), "control-rate",
                                elem_cfg.control_rate.c_str());
    }

    if (!elem_cfg.profile.empty()) {
        gst_util_set_object_arg(G_OBJECT(elem.get()), "profile", elem_cfg.profile.c_str());
    }

    // Required for RTSP streaming: insert SPS/PPS before each IDR
    g_object_set(G_OBJECT(elem.get()), "insert-sps-pps", static_cast<gboolean>(TRUE), nullptr);

    if (!gst_bin_add(GST_BIN(bin_), elem.get())) {
        LOG_E("Failed to add encoder '{}' to bin", id);
        return nullptr;
    }

    LOG_I("Built encoder '{}' (type={}, gpu_id={}, bitrate={})", id, type, elem_cfg.gpu_id,
          elem_cfg.bitrate);
    return elem.release();
}

}  // namespace engine::pipeline::builders
