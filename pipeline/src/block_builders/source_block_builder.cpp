#include "engine/pipeline/block_builders/source_block_builder.hpp"
#include "engine/pipeline/builders/muxer_builder.hpp"
#include "engine/pipeline/builders/nvmultiurisrcbin_builder.hpp"
#include "engine/pipeline/runtime_stream_manager.hpp"
#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::block_builders {

namespace {

bool expose_ghost(GstElement* bin, GstElement* target_elem, const char* pad_name,
                  const char* ghost_name) {
    engine::core::utils::GstPadPtr pad(gst_element_get_static_pad(target_elem, pad_name),
                                       gst_object_unref);
    if (!pad) {
        LOG_E("SourceBlockBuilder: no '{}' pad on '{}'", pad_name, GST_ELEMENT_NAME(target_elem));
        return false;
    }

    GstPad* ghost = gst_ghost_pad_new(ghost_name, pad.get());
    gst_pad_set_active(ghost, TRUE);
    gst_element_add_pad(bin, ghost);
    return true;
}

bool uses_manual_sources(const std::string& type) {
    return type == "nvurisrcbin";
}

bool build_multiurisrc_block(GstElement* sources_bin,
                             const engine::core::config::PipelineConfig& config) {
    builders::NvMultiUriSrcBinBuilder src_builder(sources_bin);
    GstElement* source = src_builder.build(config, 0);
    if (!source) {
        LOG_E("SourceBlockBuilder: failed to build nvmultiurisrcbin");
        return false;
    }

    if (!expose_ghost(sources_bin, source, "src", "src")) {
        return false;
    }

    return true;
}

bool build_manual_sources_block(GstElement* sources_bin,
                                const engine::core::config::PipelineConfig& config) {
    builders::MuxerBuilder muxer_builder(sources_bin);
    GstElement* muxer = muxer_builder.build(config, 0);
    if (!muxer) {
        return false;
    }

    {
        auto sources_config = config.sources;
        if (!config.pipeline.id.empty()) {
            sources_config.smart_rec_file_prefix = config.pipeline.id;
        }

        engine::pipeline::RuntimeStreamManager stream_manager(sources_bin, muxer, sources_config);
        for (const auto& camera : config.sources.cameras) {
            if (!stream_manager.add_stream(camera)) {
                LOG_E("SourceBlockBuilder: failed to add manual source '{}'", camera.id);
                return false;
            }
        }
    }

    if (!expose_ghost(sources_bin, muxer, "src", "src")) {
        return false;
    }

    LOG_I("SourceBlockBuilder: built manual sources in sources_bin with mux '{}' and {} cameras",
          GST_ELEMENT_NAME(muxer), config.sources.cameras.size());
    return true;
}

}  // namespace

SourceBlockBuilder::SourceBlockBuilder(GstElement* pipeline,
                                       std::unordered_map<std::string, GstElement*>& tails)
    : pipeline_(pipeline), tails_(tails) {}

bool SourceBlockBuilder::build(const engine::core::config::PipelineConfig& config) {
    // ── Create sources_bin ───────────────────────────────────────────────────
    const std::string sources_bin_name =
        uses_manual_sources(config.sources.type) && !config.sources.id.empty()
            ? config.sources.id
            : std::string("sources_bin");
    GstElement* sources_bin = gst_bin_new(sources_bin_name.c_str());
    if (!sources_bin) {
        LOG_E("SourceBlockBuilder: failed to create sources_bin");
        return false;
    }

    const bool ok = uses_manual_sources(config.sources.type)
                        ? build_manual_sources_block(sources_bin, config)
                        : build_multiurisrc_block(sources_bin, config);
    if (!ok) {
        gst_object_unref(sources_bin);
        return false;
    }

    // ── Add sources_bin to top-level pipeline ───────────────────────────────
    if (!gst_bin_add(GST_BIN(pipeline_), sources_bin)) {
        LOG_E("SourceBlockBuilder: failed to add sources_bin to pipeline");
        gst_object_unref(sources_bin);
        return false;
    }
    // pipeline_ owns sources_bin from here — no need to unref on error below

    tails_["src"] = sources_bin;
    LOG_I("SourceBlockBuilder: phase 1 complete ('{}', type='{}')", sources_bin_name,
          config.sources.type);
    return true;
}

}  // namespace engine::pipeline::block_builders
