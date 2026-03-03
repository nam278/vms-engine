#include "engine/pipeline/builders/msgconv_builder.hpp"
#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::builders {

MsgconvBuilder::MsgconvBuilder(GstElement* bin) : bin_(bin) {}

GstElement* MsgconvBuilder::build(const engine::core::config::PipelineConfig& config, int index) {
    // Decode compound index: output_index * 100 + element_index
    int output_idx = index / 100;
    int element_idx = index % 100;

    const auto& elem_cfg = config.outputs[output_idx].elements[element_idx];
    const auto& id = elem_cfg.id;

    auto elem = engine::core::utils::make_gst_element("nvmsgconv", id.c_str());
    if (!elem) {
        LOG_E("Failed to create nvmsgconv '{}'", id);
        return nullptr;
    }

    // nvmsgconv typically uses location field for config path
    if (!elem_cfg.location.empty()) {
        g_object_set(G_OBJECT(elem.get()), "config", elem_cfg.location.c_str(), nullptr);
    }

    // payload-type: 0=DEEPSTREAM, 1=MINIMAL
    // Using bitrate field repurposed, or default to 0
    g_object_set(G_OBJECT(elem.get()), "payload-type", static_cast<gint>(0), nullptr);

    if (!gst_bin_add(GST_BIN(bin_), elem.get())) {
        LOG_E("Failed to add nvmsgconv '{}' to bin", id);
        return nullptr;
    }

    LOG_I("Built nvmsgconv '{}'", id);
    return elem.release();
}

}  // namespace engine::pipeline::builders
