#include "engine/pipeline/builders/msgbroker_builder.hpp"
#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::builders {

MsgbrokerBuilder::MsgbrokerBuilder(GstElement* bin) : bin_(bin) {}

GstElement* MsgbrokerBuilder::build(const engine::core::config::PipelineConfig& config, int index) {
    // Decode compound index: output_index * 100 + element_index
    int output_idx = index / 100;
    int element_idx = index % 100;

    const auto& elem_cfg = config.outputs[output_idx].elements[element_idx];
    const auto& id = elem_cfg.id;

    auto elem = engine::core::utils::make_gst_element("nvmsgbroker", id.c_str());
    if (!elem) {
        LOG_E("Failed to create nvmsgbroker '{}'", id);
        return nullptr;
    }

    // Connection string (e.g., "localhost;6379")
    if (!elem_cfg.location.empty()) {
        g_object_set(G_OBJECT(elem.get()), "conn-str", elem_cfg.location.c_str(), nullptr);
    }

    // Protocol adapter library
    if (!elem_cfg.protocols.empty()) {
        g_object_set(G_OBJECT(elem.get()), "proto-lib", elem_cfg.protocols.c_str(), nullptr);
    }

    // Topic (stored in caps field for flexibility)
    if (!elem_cfg.caps.empty()) {
        g_object_set(G_OBJECT(elem.get()), "topic", elem_cfg.caps.c_str(), nullptr);
    }

    // Async publish
    g_object_set(G_OBJECT(elem.get()), "sync", static_cast<gboolean>(FALSE), nullptr);

    if (!gst_bin_add(GST_BIN(bin_), elem.get())) {
        LOG_E("Failed to add nvmsgbroker '{}' to bin", id);
        return nullptr;
    }

    LOG_I("Built nvmsgbroker '{}'", id);
    return elem.release();
}

}  // namespace engine::pipeline::builders
