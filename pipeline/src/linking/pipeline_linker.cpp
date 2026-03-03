#include "engine/pipeline/linking/pipeline_linker.hpp"
#include "engine/core/utils/logger.hpp"
#include "engine/core/utils/gst_utils.hpp"

namespace engine::pipeline::linking {

bool PipelineLinker::link(GstElement* src, GstElement* sink) {
    if (!src || !sink) {
        LOG_E("PipelineLinker::link: null element (src={}, sink={})", static_cast<void*>(src),
              static_cast<void*>(sink));
        return false;
    }

    if (!gst_element_link(src, sink)) {
        LOG_E("PipelineLinker: failed to link '{}' → '{}'", GST_ELEMENT_NAME(src),
              GST_ELEMENT_NAME(sink));
        return false;
    }

    LOG_D("PipelineLinker: linked '{}' → '{}'", GST_ELEMENT_NAME(src), GST_ELEMENT_NAME(sink));
    return true;
}

bool PipelineLinker::link_filtered(GstElement* src, GstElement* sink, const std::string& caps_str) {
    if (!src || !sink) {
        LOG_E("PipelineLinker::link_filtered: null element");
        return false;
    }

    engine::core::utils::GstCapsPtr caps(gst_caps_from_string(caps_str.c_str()), gst_caps_unref);
    if (!caps) {
        LOG_E("PipelineLinker: invalid caps string '{}'", caps_str);
        return false;
    }

    if (!gst_element_link_filtered(src, sink, caps.get())) {
        LOG_E("PipelineLinker: failed to link '{}' → '{}' with caps '{}'", GST_ELEMENT_NAME(src),
              GST_ELEMENT_NAME(sink), caps_str);
        return false;
    }

    LOG_D("PipelineLinker: linked '{}' → '{}' (caps='{}')", GST_ELEMENT_NAME(src),
          GST_ELEMENT_NAME(sink), caps_str);
    return true;
}

void PipelineLinker::connect_dynamic(GstElement* source_bin, GstElement* muxer) {
    if (!source_bin || !muxer) {
        LOG_E("PipelineLinker::connect_dynamic: null element");
        return;
    }

    g_signal_connect(source_bin, "pad-added", G_CALLBACK(on_pad_added), muxer);

    LOG_I(
        "PipelineLinker: registered dynamic pad-added callback "
        "'{}' → '{}'",
        GST_ELEMENT_NAME(source_bin), GST_ELEMENT_NAME(muxer));
}

void PipelineLinker::on_pad_added(GstElement* src, GstPad* new_pad, GstElement* muxer) {
    // Only handle video pads (ignore audio/subtitle)
    engine::core::utils::GstCapsPtr caps(gst_pad_get_current_caps(new_pad), gst_caps_unref);
    if (!caps) {
        caps.reset(gst_pad_query_caps(new_pad, nullptr));
    }

    if (caps) {
        GstStructure* structure = gst_caps_get_structure(caps.get(), 0);
        const gchar* name = gst_structure_get_name(structure);
        if (name && g_str_has_prefix(name, "audio")) {
            LOG_D("PipelineLinker: ignoring audio pad from '{}'", GST_ELEMENT_NAME(src));
            return;
        }
    }

    // Request a new sink pad on the muxer
    GstPad* sink_pad = gst_element_request_pad_simple(muxer, "sink_%u");
    if (!sink_pad) {
        LOG_E("PipelineLinker: could not get request pad from muxer '{}'", GST_ELEMENT_NAME(muxer));
        return;
    }

    if (gst_pad_is_linked(sink_pad)) {
        LOG_D("PipelineLinker: muxer sink pad already linked, skipping");
        gst_object_unref(sink_pad);
        return;
    }

    GstPadLinkReturn ret = gst_pad_link(new_pad, sink_pad);
    if (ret != GST_PAD_LINK_OK) {
        LOG_E("PipelineLinker: dynamic pad link failed ({}→{}) ret={}", GST_PAD_NAME(new_pad),
              GST_PAD_NAME(sink_pad), static_cast<int>(ret));
    } else {
        LOG_I("PipelineLinker: dynamic pad linked '{}:{}' → '{}:{}'", GST_ELEMENT_NAME(src),
              GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(muxer), GST_PAD_NAME(sink_pad));
    }

    gst_object_unref(sink_pad);
}

}  // namespace engine::pipeline::linking
