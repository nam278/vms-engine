#include "engine/pipeline/builders/infer_builder.hpp"
#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::builders {

InferBuilder::InferBuilder(GstElement* bin) : bin_(bin) {}

GstElement* InferBuilder::build(const engine::core::config::PipelineConfig& config, int index) {
    const auto& elem_cfg = config.processing.elements[index];
    const auto& id = elem_cfg.id;

    auto elem = engine::core::utils::make_gst_element("nvinfer", id.c_str());
    if (!elem) {
        LOG_E("Failed to create nvinfer '{}'", id);
        return nullptr;
    }

    g_object_set(G_OBJECT(elem.get()), "config-file-path", elem_cfg.config_file.c_str(),
                 "process-mode", static_cast<gint>(elem_cfg.process_mode), "batch-size",
                 static_cast<gint>(elem_cfg.batch_size), "gpu-id",
                 static_cast<gint>(elem_cfg.gpu_id), nullptr);
    if (elem_cfg.unique_id > 0) {
        g_object_set(G_OBJECT(elem.get()), "gie-unique-id", static_cast<gint>(elem_cfg.unique_id),
                     nullptr);
    }

    if (elem_cfg.interval > 0) {
        g_object_set(G_OBJECT(elem.get()), "interval", static_cast<gint>(elem_cfg.interval),
                     nullptr);
    }

    // SGIE-only properties
    if (elem_cfg.process_mode == 2) {
        g_object_set(G_OBJECT(elem.get()), "operate-on-gie-id",
                     static_cast<gint>(elem_cfg.operate_on_gie_id), nullptr);
        if (!elem_cfg.operate_on_class_ids.empty()) {
            g_object_set(G_OBJECT(elem.get()), "operate-on-class-ids",
                         elem_cfg.operate_on_class_ids.c_str(), nullptr);
        }
    }

    if (!gst_bin_add(GST_BIN(bin_), elem.get())) {
        LOG_E("Failed to add nvinfer '{}' to bin", id);
        return nullptr;
    }

    LOG_I("Built nvinfer '{}' (mode={}, batch={})", id, elem_cfg.process_mode, elem_cfg.batch_size);
    return elem.release();
}

}  // namespace engine::pipeline::builders
