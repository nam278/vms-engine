#include "engine/pipeline/builders/analytics_builder.hpp"
#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::builders {

AnalyticsBuilder::AnalyticsBuilder(GstElement* bin) : bin_(bin) {}

GstElement* AnalyticsBuilder::build(const engine::core::config::PipelineConfig& config, int index) {
    const auto& elem_cfg = config.processing.elements[index];
    const auto& id = elem_cfg.id;

    auto elem = engine::core::utils::make_gst_element("nvdsanalytics", id.c_str());
    if (!elem) {
        LOG_E("Failed to create nvdsanalytics '{}'", id);
        return nullptr;
    }

    if (!elem_cfg.config_file.empty()) {
        g_object_set(G_OBJECT(elem.get()), "config-file", elem_cfg.config_file.c_str(), nullptr);
    }

    g_object_set(G_OBJECT(elem.get()), "gpu-id", static_cast<gint>(elem_cfg.gpu_id), nullptr);

    if (!gst_bin_add(GST_BIN(bin_), elem.get())) {
        LOG_E("Failed to add nvdsanalytics '{}' to bin", id);
        return nullptr;
    }

    LOG_I("Built nvdsanalytics '{}' (gpu={})", id, elem_cfg.gpu_id);
    return elem.release();
}

}  // namespace engine::pipeline::builders
