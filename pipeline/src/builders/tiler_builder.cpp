#include "engine/pipeline/builders/tiler_builder.hpp"
#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::builders {

TilerBuilder::TilerBuilder(GstElement* bin) : bin_(bin) {}

GstElement* TilerBuilder::build(const engine::core::config::PipelineConfig& config, int index) {
    const auto& elem_cfg = config.visuals.elements[index];
    const auto& id = elem_cfg.id;

    auto elem = engine::core::utils::make_gst_element("nvmultistreamtiler", id.c_str());
    if (!elem) {
        LOG_E("Failed to create nvmultistreamtiler '{}'", id);
        return nullptr;
    }

    g_object_set(G_OBJECT(elem.get()), "rows", static_cast<guint>(elem_cfg.rows), "columns",
                 static_cast<guint>(elem_cfg.columns), "width", static_cast<guint>(elem_cfg.width),
                 "height", static_cast<guint>(elem_cfg.height), "gpu-id",
                 static_cast<gint>(elem_cfg.gpu_id), nullptr);

    if (!gst_bin_add(GST_BIN(bin_), elem.get())) {
        LOG_E("Failed to add nvmultistreamtiler '{}' to bin", id);
        return nullptr;
    }

    LOG_I("Built nvmultistreamtiler '{}' ({}x{} grid, {}x{} output)", id, elem_cfg.rows,
          elem_cfg.columns, elem_cfg.width, elem_cfg.height);
    return elem.release();
}

}  // namespace engine::pipeline::builders
