#include "engine/pipeline/builders/sink_builder.hpp"
#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::builders {

SinkBuilder::SinkBuilder(GstElement* bin) : bin_(bin) {}

GstElement* SinkBuilder::build(const engine::core::config::PipelineConfig& config, int index) {
    // Decode compound index: output_index * 100 + element_index
    int output_idx = index / 100;
    int element_idx = index % 100;

    const auto& elem_cfg = config.outputs[output_idx].elements[element_idx];
    const auto& id = elem_cfg.id;
    const auto& type = elem_cfg.type;

    auto elem = engine::core::utils::make_gst_element(type.c_str(), id.c_str());
    if (!elem) {
        LOG_E("Failed to create sink '{}' (type={})", id, type);
        return nullptr;
    }

    if (type == "rtspclientsink") {
        if (!elem_cfg.location.empty()) {
            g_object_set(G_OBJECT(elem.get()), "location", elem_cfg.location.c_str(), nullptr);
        }
        if (!elem_cfg.protocols.empty()) {
            g_object_set(G_OBJECT(elem.get()), "protocols", elem_cfg.protocols.c_str(), nullptr);
        }
    } else if (type == "filesink") {
        if (!elem_cfg.location.empty()) {
            g_object_set(G_OBJECT(elem.get()), "location", elem_cfg.location.c_str(), nullptr);
        }
    } else if (type == "fakesink") {
        g_object_set(G_OBJECT(elem.get()), "sync", static_cast<gboolean>(FALSE), nullptr);
    }

    if (!gst_bin_add(GST_BIN(bin_), elem.get())) {
        LOG_E("Failed to add sink '{}' to bin", id);
        return nullptr;
    }

    LOG_I("Built sink '{}' (type={})", id, type);
    return elem.release();
}

}  // namespace engine::pipeline::builders
