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
    return build(elem_cfg.id, elem_cfg.gpu_id, elem_cfg.nvbuf_memory_type, elem_cfg.src_crop,
                 elem_cfg.dest_crop);
}

GstElement* VideoConvertBuilder::build(const std::string& name, int gpu_id,
                                       const std::string& nvbuf_memory_type,
                                       const std::string& src_crop, const std::string& dest_crop) {
    auto elem = engine::core::utils::make_gst_element("nvvideoconvert", name.c_str());
    if (!elem) {
        LOG_E("Failed to create nvvideoconvert '{}'", name);
        return nullptr;
    }

    g_object_set(G_OBJECT(elem.get()), "gpu-id", static_cast<gint>(gpu_id), nullptr);

    // String-valued enum-like properties are applied via gst_util_set_object_arg().
    if (!nvbuf_memory_type.empty()) {
        gst_util_set_object_arg(G_OBJECT(elem.get()), "nvbuf-memory-type",
                                nvbuf_memory_type.c_str());
    }
    if (should_apply_crop(src_crop)) {
        g_object_set(G_OBJECT(elem.get()), "src-crop", src_crop.c_str(), nullptr);
    } else if (!src_crop.empty()) {
        LOG_W("VideoConvertBuilder: skipping src-crop '{}' for '{}' because width/height are 0",
              src_crop, name);
    }
    if (should_apply_crop(dest_crop)) {
        g_object_set(G_OBJECT(elem.get()), "dest-crop", dest_crop.c_str(), nullptr);
    } else if (!dest_crop.empty()) {
        LOG_W("VideoConvertBuilder: skipping dest-crop '{}' for '{}' because width/height are 0",
              dest_crop, name);
    }

    if (!gst_bin_add(GST_BIN(bin_), elem.get())) {
        LOG_E("Failed to add nvvideoconvert '{}' to bin", name);
        return nullptr;
    }

    LOG_I("Built nvvideoconvert '{}' (gpu_id={})", name, gpu_id);
    return elem.release();
}

}  // namespace engine::pipeline::builders