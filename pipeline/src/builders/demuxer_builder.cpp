#include "engine/pipeline/builders/demuxer_builder.hpp"
#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::builders {

DemuxerBuilder::DemuxerBuilder(GstElement* bin) : bin_(bin) {}

GstElement* DemuxerBuilder::build(const engine::core::config::PipelineConfig& config,
                                  int /*index*/) {
    const std::string id = "nvstreamdemux0";

    auto elem = engine::core::utils::make_gst_element("nvstreamdemux", id.c_str());
    if (!elem) {
        LOG_E("Failed to create nvstreamdemux '{}'", id);
        return nullptr;
    }

    if (!gst_bin_add(GST_BIN(bin_), elem.get())) {
        LOG_E("Failed to add nvstreamdemux '{}' to bin", id);
        return nullptr;
    }

    LOG_I("Built nvstreamdemux '{}'", id);
    return elem.release();
}

}  // namespace engine::pipeline::builders
