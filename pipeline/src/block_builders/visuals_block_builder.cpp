#include "engine/pipeline/block_builders/visuals_block_builder.hpp"
#include "engine/pipeline/builders/tiler_builder.hpp"
#include "engine/pipeline/builders/osd_builder.hpp"
#include "engine/pipeline/builders/queue_builder.hpp"
#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::block_builders {

// ── Helper: expose a ghost pad on a bin pointing at target_elem's pad ───────────────
static bool expose_ghost(GstElement* bin, GstElement* target_elem, const char* pad_name,
                         const char* ghost_name) {
    engine::core::utils::GstPadPtr pad(gst_element_get_static_pad(target_elem, pad_name),
                                       gst_object_unref);
    if (!pad) {
        LOG_E("expose_ghost: no '{}' pad on '{}'", pad_name, GST_ELEMENT_NAME(target_elem));
        return false;
    }
    GstPad* ghost = gst_ghost_pad_new(ghost_name, pad.get());
    gst_pad_set_active(ghost, TRUE);
    gst_element_add_pad(bin, ghost);
    return true;
}

VisualsBlockBuilder::VisualsBlockBuilder(GstElement* pipeline,
                                         std::unordered_map<std::string, GstElement*>& tails)
    : pipeline_(pipeline), tails_(tails) {}

bool VisualsBlockBuilder::build(const engine::core::config::PipelineConfig& config) {
    if (!config.visuals.enable) {
        LOG_I("VisualsBlockBuilder: visuals disabled, skipping");
        tails_["vis"] = tails_["proc"];
        return true;
    }

    const auto& vis = config.visuals;
    if (vis.elements.empty()) {
        LOG_W("VisualsBlockBuilder: visuals enabled but no elements configured");
        tails_["vis"] = tails_["proc"];
        return true;
    }

    // ── Create visuals_bin ───────────────────────────────────────────────────────
    GstElement* vis_bin = gst_bin_new("visuals_bin");
    if (!vis_bin) {
        LOG_E("VisualsBlockBuilder: failed to create visuals_bin");
        return false;
    }

    GstElement* first_elem = nullptr;  // ← ghost sink target
    GstElement* prev = nullptr;        // running link tail inside vis_bin
    builders::QueueBuilder q_builder(vis_bin);

    for (int i = 0; i < static_cast<int>(vis.elements.size()); ++i) {
        const auto& elem_cfg = vis.elements[i];

        // Insert queue BEFORE element (if queue: {} configured on this element)
        if (elem_cfg.has_queue) {
            std::string q_name = elem_cfg.id + "_queue";
            GstElement* q = q_builder.build(elem_cfg.queue, q_name);
            if (!q) {
                LOG_E("VisualsBlockBuilder: failed to build queue for '{}'", elem_cfg.id);
                gst_object_unref(vis_bin);
                return false;
            }
            if (!first_elem)
                first_elem = q;
            if (prev && !gst_element_link(prev, q)) {
                LOG_E("VisualsBlockBuilder: link failed {} → {}", GST_ELEMENT_NAME(prev), q_name);
                gst_object_unref(vis_bin);
                return false;
            }
            prev = q;
        }

        GstElement* elem = nullptr;
        if (elem_cfg.type == "nvmultistreamtiler") {
            builders::TilerBuilder builder(vis_bin);
            elem = builder.build(config, i);
        } else if (elem_cfg.type == "nvdsosd") {
            builders::OsdBuilder builder(vis_bin);
            elem = builder.build(config, i);
        } else {
            LOG_E("VisualsBlockBuilder: unknown type '{}'", elem_cfg.type);
            gst_object_unref(vis_bin);
            return false;
        }

        if (!elem) {
            LOG_E("VisualsBlockBuilder: failed to build '{}'", elem_cfg.id);
            gst_object_unref(vis_bin);
            return false;
        }
        if (!first_elem)
            first_elem = elem;
        if (prev && !gst_element_link(prev, elem)) {
            LOG_E("VisualsBlockBuilder: link failed {} → {}", GST_ELEMENT_NAME(prev),
                  GST_ELEMENT_NAME(elem));
            gst_object_unref(vis_bin);
            return false;
        }
        prev = elem;
    }

    if (!first_elem || !prev) {
        LOG_E("VisualsBlockBuilder: empty element chain");
        gst_object_unref(vis_bin);
        return false;
    }

    // ── Expose ghost sink (first element) + ghost src (last element) ─────────────────
    if (!expose_ghost(vis_bin, first_elem, "sink", "sink") ||
        !expose_ghost(vis_bin, prev, "src", "src")) {
        gst_object_unref(vis_bin);
        return false;
    }

    // ── Add vis_bin to pipeline, link processing_bin → visuals_bin ──────────────────
    if (!gst_bin_add(GST_BIN(pipeline_), vis_bin)) {
        LOG_E("VisualsBlockBuilder: failed to add visuals_bin to pipeline");
        gst_object_unref(vis_bin);
        return false;
    }
    if (!gst_element_link(tails_["proc"], vis_bin)) {
        LOG_E("VisualsBlockBuilder: failed to link processing_bin → visuals_bin");
        return false;
    }

    tails_["vis"] = vis_bin;
    LOG_I("VisualsBlockBuilder: phase 3 complete (visuals_bin, last='{}')", GST_ELEMENT_NAME(prev));
    return true;
}

}  // namespace engine::pipeline::block_builders
