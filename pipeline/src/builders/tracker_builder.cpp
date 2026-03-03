#include "engine/pipeline/builders/tracker_builder.hpp"
#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::builders {

TrackerBuilder::TrackerBuilder(GstElement* bin) : bin_(bin) {}

GstElement* TrackerBuilder::build(const engine::core::config::PipelineConfig& config, int index) {
    const auto& elem_cfg = config.processing.elements[index];
    const auto& id = elem_cfg.id;

    auto elem = engine::core::utils::make_gst_element("nvtracker", id.c_str());
    if (!elem) {
        LOG_E("Failed to create nvtracker '{}'", id);
        return nullptr;
    }

    g_object_set(G_OBJECT(elem.get()), "tracker-width", static_cast<gint>(elem_cfg.tracker_width),
                 "tracker-height", static_cast<gint>(elem_cfg.tracker_height), "gpu-id",
                 static_cast<gint>(elem_cfg.gpu_id), "compute-hw",
                 static_cast<gint>(elem_cfg.compute_hw), "display-tracking-id",
                 static_cast<gboolean>(elem_cfg.display_tracking_id), nullptr);

    if (!elem_cfg.ll_lib_file.empty()) {
        g_object_set(G_OBJECT(elem.get()), "ll-lib-file", elem_cfg.ll_lib_file.c_str(), nullptr);
    }
    if (!elem_cfg.ll_config_file.empty()) {
        g_object_set(G_OBJECT(elem.get()), "ll-config-file", elem_cfg.ll_config_file.c_str(),
                     nullptr);
    }
    if (elem_cfg.user_meta_pool_size > 0) {
        g_object_set(G_OBJECT(elem.get()), "user-meta-pool-size",
                     static_cast<guint>(elem_cfg.user_meta_pool_size), nullptr);
    }

    if (!gst_bin_add(GST_BIN(bin_), elem.get())) {
        LOG_E("Failed to add nvtracker '{}' to bin", id);
        return nullptr;
    }

    LOG_I("Built nvtracker '{}' ({}x{}, compute_hw={})", id, elem_cfg.tracker_width,
          elem_cfg.tracker_height, elem_cfg.compute_hw);
    return elem.release();
}

}  // namespace engine::pipeline::builders
