#include "engine/pipeline/builders/osd_builder.hpp"
#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::builders {

OsdBuilder::OsdBuilder(GstElement* bin) : bin_(bin) {}

GstElement* OsdBuilder::build(const engine::core::config::PipelineConfig& config, int index) {
    const auto& elem_cfg = config.visuals.elements[index];
    const auto& id = elem_cfg.id;

    auto elem = engine::core::utils::make_gst_element("nvdsosd", id.c_str());
    if (!elem) {
        LOG_E("Failed to create nvdsosd '{}'", id);
        return nullptr;
    }

    g_object_set(G_OBJECT(elem.get()), "process-mode", static_cast<gint>(elem_cfg.process_mode),
                 "display-bbox", static_cast<gboolean>(elem_cfg.display_bbox), "display-text",
                 static_cast<gboolean>(elem_cfg.display_text), "display-mask",
                 static_cast<gboolean>(elem_cfg.display_mask), "border-width",
                 static_cast<gint>(elem_cfg.border_width), "gpu-id",
                 static_cast<gint>(elem_cfg.gpu_id), nullptr);

    if (!gst_bin_add(GST_BIN(bin_), elem.get())) {
        LOG_E("Failed to add nvdsosd '{}' to bin", id);
        return nullptr;
    }

    LOG_I("Built nvdsosd '{}' (mode={}, bbox={}, text={})", id, elem_cfg.process_mode,
          elem_cfg.display_bbox, elem_cfg.display_text);
    return elem.release();
}

}  // namespace engine::pipeline::builders
