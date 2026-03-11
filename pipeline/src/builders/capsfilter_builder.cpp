#include "engine/pipeline/builders/capsfilter_builder.hpp"

#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::builders {

CapsFilterBuilder::CapsFilterBuilder(GstElement* bin) : bin_(bin) {}

GstElement* CapsFilterBuilder::build(const engine::core::config::PipelineConfig& config,
                                     int index) {
    // Output builders share the existing compound index convention used by encoder/sink builders.
    const int output_idx = index / 100;
    const int element_idx = index % 100;

    const auto& elem_cfg = config.outputs[output_idx].elements[element_idx];
    return build(elem_cfg.id, elem_cfg.caps);
}

GstElement* CapsFilterBuilder::build(const std::string& name, const std::string& caps_string) {
    auto elem = engine::core::utils::make_gst_element("capsfilter", name.c_str());
    if (!elem) {
        LOG_E("Failed to create capsfilter '{}'", name);
        return nullptr;
    }

    if (!caps_string.empty()) {
        // Keep caps in an RAII wrapper until g_object_set() has taken its own reference.
        engine::core::utils::GstCapsPtr caps(gst_caps_from_string(caps_string.c_str()),
                                             gst_caps_unref);
        if (!caps) {
            LOG_E("Failed to parse caps '{}' for '{}'", caps_string, name);
            return nullptr;
        }
        g_object_set(G_OBJECT(elem.get()), "caps", caps.get(), nullptr);
    } else {
        LOG_W("CapsFilterBuilder: no caps configured for '{}'", name);
    }

    if (!gst_bin_add(GST_BIN(bin_), elem.get())) {
        LOG_E("Failed to add capsfilter '{}' to bin", name);
        return nullptr;
    }

    LOG_I("Built capsfilter '{}'", name);
    return elem.release();
}

}  // namespace engine::pipeline::builders