#include "engine/pipeline/builders/parser_builder.hpp"

#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::builders {

ParserBuilder::ParserBuilder(GstElement* bin) : bin_(bin) {}

GstElement* ParserBuilder::build(const engine::core::config::PipelineConfig& config, int index) {
    // Output builders share the existing compound index convention used by encoder/sink builders.
    const int output_idx = index / 100;
    const int element_idx = index % 100;

    const auto& elem_cfg = config.outputs[output_idx].elements[element_idx];
    const auto& id = elem_cfg.id;
    const auto& type = elem_cfg.type;

    // Parser type comes from YAML so the same builder handles both h264parse and h265parse.
    auto elem = engine::core::utils::make_gst_element(type.c_str(), id.c_str());
    if (!elem) {
        LOG_E("Failed to create parser '{}' (type={})", id, type);
        return nullptr;
    }

    if (!gst_bin_add(GST_BIN(bin_), elem.get())) {
        LOG_E("Failed to add parser '{}' to bin", id);
        return nullptr;
    }

    LOG_I("Built parser '{}' (type={})", id, type);
    return elem.release();
}

}  // namespace engine::pipeline::builders