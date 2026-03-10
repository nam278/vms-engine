#include "engine/pipeline/builders/video_convert_builder.hpp"

#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

#include <cstdio>

namespace {

bool should_apply_crop(const std::string& crop) {
    if (crop.empty()) {
        return false;
    }

    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    if (std::sscanf(crop.c_str(), "%d:%d:%d:%d", &left, &top, &width, &height) == 4) {
        return width > 0 && height > 0;
    }

    return true;
}

}  // namespace

namespace engine::pipeline::builders {

VideoConvertBuilder::VideoConvertBuilder(GstElement* bin) : bin_(bin) {}

GstElement* VideoConvertBuilder::build(const engine::core::config::PipelineConfig& config,
                                       int index) {
    // Output builders share the existing compound index convention used by encoder/sink builders.
    const int output_idx = index / 100;
    const int element_idx = index % 100;

    const auto& elem_cfg = config.outputs[output_idx].elements[element_idx];
    const auto& id = elem_cfg.id;

    auto elem = engine::core::utils::make_gst_element("nvvideoconvert", id.c_str());
    if (!elem) {
        LOG_E("Failed to create nvvideoconvert '{}'", id);
        return nullptr;
    }

    g_object_set(G_OBJECT(elem.get()), "gpu-id", static_cast<gint>(elem_cfg.gpu_id), nullptr);

    // String-valued enum-like properties are applied via gst_util_set_object_arg().
    if (!elem_cfg.nvbuf_memory_type.empty()) {
        gst_util_set_object_arg(G_OBJECT(elem.get()), "nvbuf-memory-type",
                                elem_cfg.nvbuf_memory_type.c_str());
    }
    if (should_apply_crop(elem_cfg.src_crop)) {
        g_object_set(G_OBJECT(elem.get()), "src-crop", elem_cfg.src_crop.c_str(), nullptr);
    } else if (!elem_cfg.src_crop.empty()) {
        LOG_W("VideoConvertBuilder: skipping src-crop '{}' for '{}' because width/height are 0",
              elem_cfg.src_crop, id);
    }
    if (should_apply_crop(elem_cfg.dest_crop)) {
        g_object_set(G_OBJECT(elem.get()), "dest-crop", elem_cfg.dest_crop.c_str(), nullptr);
    } else if (!elem_cfg.dest_crop.empty()) {
        LOG_W("VideoConvertBuilder: skipping dest-crop '{}' for '{}' because width/height are 0",
              elem_cfg.dest_crop, id);
    }

    if (!gst_bin_add(GST_BIN(bin_), elem.get())) {
        LOG_E("Failed to add nvvideoconvert '{}' to bin", id);
        return nullptr;
    }

    LOG_I("Built nvvideoconvert '{}' (gpu_id={})", id, elem_cfg.gpu_id);
    return elem.release();
}

}  // namespace engine::pipeline::builders