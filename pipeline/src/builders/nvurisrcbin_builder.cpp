#include "engine/pipeline/builders/nvurisrcbin_builder.hpp"

#include "engine/pipeline/builders/capsfilter_builder.hpp"
#include "engine/pipeline/builders/queue_builder.hpp"
#include "engine/pipeline/builders/video_convert_builder.hpp"

#include "engine/pipeline/source_naming.hpp"
#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::builders {

namespace {

struct SourcePadAddedContext {
    GstPad* downstream_sink_pad = nullptr;
    GstPad* source_bin_ghost_pad = nullptr;
};

void destroy_source_pad_added_context(gpointer data) {
    auto* context = static_cast<SourcePadAddedContext*>(data);
    if (context == nullptr) {
        return;
    }

    if (context->downstream_sink_pad != nullptr) {
        gst_object_unref(context->downstream_sink_pad);
    }
    if (context->source_bin_ghost_pad != nullptr) {
        gst_object_unref(context->source_bin_ghost_pad);
    }
    delete context;
}

void on_nvurisrcbin_pad_added(GstElement* source, GstPad* new_pad, gpointer user_data) {
    auto* context = static_cast<SourcePadAddedContext*>(user_data);
    if (context == nullptr || gst_pad_get_direction(new_pad) != GST_PAD_SRC) {
        return;
    }

    if (context->downstream_sink_pad != nullptr &&
        !gst_pad_is_linked(context->downstream_sink_pad)) {
        const GstPadLinkReturn link_result = gst_pad_link(new_pad, context->downstream_sink_pad);
        if (link_result != GST_PAD_LINK_OK) {
            LOG_E("NvUriSrcBinBuilder: failed to link nvurisrcbin pad '{}' ({})",
                  GST_PAD_NAME(new_pad), gst_pad_link_get_name(link_result));
        }
        return;
    }

    if (context->source_bin_ghost_pad != nullptr) {
        GstPad* target = gst_ghost_pad_get_target(GST_GHOST_PAD(context->source_bin_ghost_pad));
        if (target != nullptr) {
            gst_object_unref(target);
            return;
        }

        if (!gst_ghost_pad_set_target(GST_GHOST_PAD(context->source_bin_ghost_pad), new_pad)) {
            LOG_E("NvUriSrcBinBuilder: failed to set source-bin ghost target from '{}'",
                  GST_PAD_NAME(new_pad));
        }
    }

    const gchar* element_name = source != nullptr ? GST_ELEMENT_NAME(source) : "unknown";
    LOG_D("NvUriSrcBinBuilder: pad-added handled for '{}': {}", element_name,
          GST_PAD_NAME(new_pad));
}

GstElement* build_source_branch_element(GstElement* source_bin, const std::string& camera_id,
                                        const engine::core::config::SourceBranchElementConfig& cfg,
                                        int default_gpu_id) {
    const std::string element_name = make_source_branch_element_name(camera_id, cfg.id, cfg.type);

    if (cfg.type == "nvvideoconvert") {
        VideoConvertBuilder builder(source_bin);
        return builder.build(element_name, cfg.gpu_id > 0 ? cfg.gpu_id : default_gpu_id,
                             cfg.nvbuf_memory_type, cfg.src_crop, cfg.dest_crop);
    }

    if (cfg.type == "capsfilter") {
        CapsFilterBuilder builder(source_bin);
        return builder.build(element_name, cfg.caps);
    }

    if (cfg.type == "queue") {
        QueueBuilder builder(source_bin);
        return builder.build(cfg.queue, element_name);
    }

    LOG_E("NvUriSrcBinBuilder: unsupported source-branch element type '{}'", cfg.type);
    return nullptr;
}

}  // namespace

NvUriSrcBinBuilder::NvUriSrcBinBuilder(GstElement* bin) : bin_(bin) {}

GstElement* NvUriSrcBinBuilder::build(const engine::core::config::PipelineConfig& config,
                                      int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= config.sources.cameras.size()) {
        LOG_E("NvUriSrcBinBuilder: invalid camera index {}", index);
        return nullptr;
    }

    auto sources = config.sources;
    if (!config.pipeline.id.empty()) {
        sources.smart_rec_file_prefix = config.pipeline.id;
    }

    return build(sources, config.sources.cameras[static_cast<std::size_t>(index)],
                 static_cast<uint32_t>(index));
}

GstElement* NvUriSrcBinBuilder::build(const engine::core::config::SourcesConfig& sources,
                                      const engine::core::config::CameraConfig& camera,
                                      uint32_t source_id) {
    if (camera.id.empty() || camera.uri.empty()) {
        LOG_E("NvUriSrcBinBuilder: camera id and uri are required");
        return nullptr;
    }

    const std::string source_bin_name = make_source_bin_name(camera.id);
    auto source_bin =
        engine::core::utils::GstElementPtr(gst_bin_new(source_bin_name.c_str()), gst_object_unref);
    if (!source_bin) {
        LOG_E("Failed to create source bin '{}'", source_bin_name);
        return nullptr;
    }

    const std::string source_name = make_source_element_name(camera.id);
    auto source = engine::core::utils::make_gst_element("nvurisrcbin", source_name.c_str());
    if (!source) {
        LOG_E("Failed to create nvurisrcbin '{}'", source_name);
        return nullptr;
    }

    g_object_set(G_OBJECT(source.get()), "uri", camera.uri.c_str(), "gpu-id",
                 static_cast<gint>(sources.gpu_id), "num-extra-surfaces",
                 static_cast<gint>(sources.num_extra_surfaces), "cudadec-memtype",
                 static_cast<gint>(sources.cudadec_memtype), "dec-skip-frames",
                 static_cast<gint>(sources.dec_skip_frames), "drop-frame-interval",
                 static_cast<gint>(sources.drop_frame_interval), "select-rtp-protocol",
                 static_cast<gint>(sources.select_rtp_protocol), "rtsp-reconnect-interval",
                 static_cast<gint>(sources.rtsp_reconnect_interval), "rtsp-reconnect-attempts",
                 static_cast<gint>(sources.rtsp_reconnect_attempts), "latency",
                 static_cast<guint>(sources.latency), "udp-buffer-size",
                 static_cast<guint>(sources.udp_buffer_size), "source-id",
                 static_cast<gint>(source_id), "disable-audio",
                 static_cast<gboolean>(sources.disable_audio), "disable-passthrough",
                 static_cast<gboolean>(sources.disable_passthrough), "file-loop",
                 static_cast<gboolean>(sources.file_loop), "async-handling",
                 static_cast<gboolean>(sources.async_handling), "low-latency-mode",
                 static_cast<gboolean>(sources.low_latency_mode), nullptr);

    if (sources.drop_on_latency.has_value()) {
        g_object_set(G_OBJECT(source.get()), "drop-on-latency",
                     static_cast<gboolean>(*sources.drop_on_latency), nullptr);
    }

    const int init_reconnect_interval = sources.init_rtsp_reconnect_interval >= 0
                                            ? sources.init_rtsp_reconnect_interval
                                            : sources.rtsp_reconnect_interval;
    if (init_reconnect_interval > 0) {
        g_object_set(G_OBJECT(source.get()), "init-rtsp-reconnect-interval",
                     static_cast<gint>(init_reconnect_interval), nullptr);
    }

    if (sources.smart_record > 0) {
        g_object_set(G_OBJECT(source.get()), "smart-record",
                     static_cast<gint>(sources.smart_record), "smart-rec-dir-path",
                     sources.smart_rec_dir_path.c_str(), "smart-rec-file-prefix",
                     sources.smart_rec_file_prefix.c_str(), "smart-rec-cache",
                     static_cast<gint>(sources.smart_rec_cache), "smart-rec-default-duration",
                     static_cast<gint>(sources.smart_rec_default_duration), "smart-rec-mode",
                     static_cast<gint>(sources.smart_rec_mode), "smart-rec-container",
                     static_cast<gint>(sources.smart_rec_container), nullptr);

        LOG_D("NvUriSrcBinBuilder: smart-record uses prefix '{}' and source-id {} for '{}'",
              sources.smart_rec_file_prefix, source_id, camera.id);
    }

    if (!gst_bin_add(GST_BIN(source_bin.get()), source.get())) {
        LOG_E("Failed to add nvurisrcbin '{}' to source bin", source_name);
        return nullptr;
    }

    GstElement* source_elem = source.release();

    GstElement* tail = source_elem;
    GstElement* first_downstream = nullptr;
    for (const auto& branch_elem : sources.branch.elements) {
        if (!branch_elem.enabled) {
            continue;
        }

        GstElement* child =
            build_source_branch_element(source_bin.get(), camera.id, branch_elem, sources.gpu_id);
        if (child == nullptr) {
            return nullptr;
        }

        if (first_downstream == nullptr) {
            first_downstream = child;
        }

        if (tail != source_elem) {
            if (!gst_element_link(tail, child)) {
                LOG_E("Failed to link '{}' -> '{}' inside source bin '{}'", GST_ELEMENT_NAME(tail),
                      GST_ELEMENT_NAME(child), source_bin_name);
                return nullptr;
            }
        }

        tail = child;
    }

    GstPad* ghost_src = nullptr;
    engine::core::utils::GstPadPtr tail_src_pad(nullptr, gst_object_unref);
    if (tail != source_elem) {
        tail_src_pad.reset(gst_element_get_static_pad(tail, "src"));
        if (!tail_src_pad) {
            LOG_E("NvUriSrcBinBuilder: no static 'src' pad on '{}'", GST_ELEMENT_NAME(tail));
            return nullptr;
        }

        ghost_src = gst_ghost_pad_new("src", tail_src_pad.get());
    } else {
        GstPad* direct_src_pad = gst_element_get_static_pad(source_elem, "src");
        if (direct_src_pad != nullptr) {
            tail_src_pad.reset(direct_src_pad);
            ghost_src = gst_ghost_pad_new("src", tail_src_pad.get());
        } else {
            ghost_src = gst_ghost_pad_new_no_target("src", GST_PAD_SRC);
        }
    }

    if (ghost_src == nullptr) {
        LOG_E("NvUriSrcBinBuilder: failed to create source-bin ghost src for '{}'",
              source_bin_name);
        return nullptr;
    }

    gst_pad_set_active(ghost_src, TRUE);
    if (!gst_element_add_pad(source_bin.get(), ghost_src)) {
        gst_object_unref(ghost_src);
        LOG_E("NvUriSrcBinBuilder: failed to add ghost src pad to '{}'", source_bin_name);
        return nullptr;
    }

    auto* pad_context = new SourcePadAddedContext{};
    if (first_downstream != nullptr) {
        pad_context->downstream_sink_pad = gst_element_get_static_pad(first_downstream, "sink");
        if (pad_context->downstream_sink_pad == nullptr) {
            delete pad_context;
            LOG_E("NvUriSrcBinBuilder: no static 'sink' pad on '{}'",
                  GST_ELEMENT_NAME(first_downstream));
            return nullptr;
        }
    } else {
        pad_context->source_bin_ghost_pad = GST_PAD(gst_object_ref(ghost_src));
    }
    g_object_set_data_full(G_OBJECT(source_elem), "engine_source_pad_context", pad_context,
                           destroy_source_pad_added_context);
    g_signal_connect(source_elem, "pad-added", G_CALLBACK(on_nvurisrcbin_pad_added), pad_context);

    if (first_downstream != nullptr) {
        GstPad* source_pad = gst_element_get_static_pad(source_elem, "src");
        if (source_pad != nullptr) {
            on_nvurisrcbin_pad_added(source_elem, source_pad, pad_context);
            gst_object_unref(source_pad);
        }
    }

    if (!gst_bin_add(GST_BIN(bin_), source_bin.get())) {
        LOG_E("Failed to add source bin '{}' to parent bin", source_bin_name);
        return nullptr;
    }

    LOG_I("Built source bin '{}' for camera '{}' with {} branch element(s)", source_bin_name,
          camera.id, sources.branch.elements.size());
    return source_bin.release();
}

}  // namespace engine::pipeline::builders