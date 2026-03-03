#include "engine/pipeline/block_builders/source_block_builder.hpp"
#include "engine/pipeline/builders/source_builder.hpp"
#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::block_builders {

SourceBlockBuilder::SourceBlockBuilder(GstElement* pipeline,
                                       std::unordered_map<std::string, GstElement*>& tails)
    : pipeline_(pipeline), tails_(tails) {}

bool SourceBlockBuilder::build(const engine::core::config::PipelineConfig& config) {
    // ── Create sources_bin ───────────────────────────────────────────────────
    GstElement* sources_bin = gst_bin_new("sources_bin");
    if (!sources_bin) {
        LOG_E("SourceBlockBuilder: failed to create sources_bin");
        return false;
    }

    // Build nvmultiurisrcbin inside sources_bin (SourceBuilder adds it to sources_bin)
    builders::SourceBuilder src_builder(sources_bin);
    GstElement* source = src_builder.build(config, 0);
    if (!source) {
        LOG_E("SourceBlockBuilder: failed to build nvmultiurisrcbin");
        gst_object_unref(sources_bin);
        return false;
    }

    // ── Expose ghost src pad ───────────────────────────────────────────────
    // nvmultiurisrcbin exposes the batched muxer output as a static "src" ghost pad.
    engine::core::utils::GstPadPtr src_pad(gst_element_get_static_pad(source, "src"),
                                           gst_object_unref);
    if (!src_pad) {
        LOG_E("SourceBlockBuilder: nvmultiurisrcbin has no static 'src' pad");
        gst_object_unref(sources_bin);
        return false;
    }
    GstPad* ghost_src = gst_ghost_pad_new("src", src_pad.get());
    gst_pad_set_active(ghost_src, TRUE);
    gst_element_add_pad(sources_bin, ghost_src);
    LOG_D("SourceBlockBuilder: exposed ghost src pad on sources_bin");

    // ── Add sources_bin to top-level pipeline ───────────────────────────────
    if (!gst_bin_add(GST_BIN(pipeline_), sources_bin)) {
        LOG_E("SourceBlockBuilder: failed to add sources_bin to pipeline");
        gst_object_unref(sources_bin);
        return false;
    }
    // pipeline_ owns sources_bin from here — no need to unref on error below

    tails_["src"] = sources_bin;
    LOG_I("SourceBlockBuilder: phase 1 complete (sources_bin)");
    return true;
}

}  // namespace engine::pipeline::block_builders
