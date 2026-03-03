#include "engine/pipeline/probes/probe_handler_manager.hpp"
#include "engine/core/utils/logger.hpp"

#include <gstnvdsmeta.h>

namespace engine::pipeline::probes {

ProbeHandlerManager::ProbeHandlerManager(
    std::shared_ptr<engine::core::handlers::IHandlerManager> handler_manager, GstElement* pipeline)
    : handler_manager_(std::move(handler_manager)), pipeline_(pipeline) {}

bool ProbeHandlerManager::attach_probes(
    const std::vector<engine::core::config::EventHandlerConfig>& configs) {
    for (const auto& cfg : configs) {
        if (!cfg.enable) {
            LOG_D("ProbeHandlerManager: handler '{}' disabled, skipping", cfg.id);
            continue;
        }

        if (cfg.probe_element.empty()) {
            LOG_W("ProbeHandlerManager: handler '{}' has no probe_element", cfg.id);
            continue;
        }

        GstElement* element = find_element(cfg.probe_element);
        if (!element) {
            LOG_E("ProbeHandlerManager: element '{}' not found for handler '{}'", cfg.probe_element,
                  cfg.id);
            return false;
        }

        GstPad* pad = gst_element_get_static_pad(element, "src");
        if (!pad) {
            LOG_E("ProbeHandlerManager: no src pad on '{}' for handler '{}'", cfg.probe_element,
                  cfg.id);
            return false;
        }

        // Capture handler_manager raw pointer + trigger string for the C callback
        struct ProbeUserData {
            engine::core::handlers::IHandlerManager* mgr;
            std::string trigger;
        };
        auto* udata = new ProbeUserData{handler_manager_.get(), cfg.trigger};

        gulong probe_id = gst_pad_add_probe(
            pad, GST_PAD_PROBE_TYPE_BUFFER,
            [](GstPad* /*pad*/, GstPadProbeInfo* info, gpointer user_data) -> GstPadProbeReturn {
                auto* data = static_cast<ProbeUserData*>(user_data);
                GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
                NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buf);
                if (!batch_meta)
                    return GST_PAD_PROBE_OK;

                for (NvDsMetaList* l_frame = batch_meta->frame_meta_list; l_frame;
                     l_frame = l_frame->next) {
                    auto* frame_meta = static_cast<NvDsFrameMeta*>(l_frame->data);

                    engine::core::handlers::HandlerContext ctx;
                    ctx.event_type = data->trigger;
                    ctx.source_id = static_cast<int>(frame_meta->source_id);
                    ctx.frame_number = frame_meta->frame_num;
                    // ctx.data could carry NvDsFrameMeta* for handlers
                    data->mgr->dispatch(ctx);
                }
                return GST_PAD_PROBE_OK;
            },
            udata, [](gpointer user_data) { delete static_cast<ProbeUserData*>(user_data); });

        probes_.push_back({element, pad, probe_id, cfg.id});
        LOG_I(
            "ProbeHandlerManager: attached probe on '{}' for handler '{}' "
            "(probe_id={})",
            cfg.probe_element, cfg.id, probe_id);
    }

    LOG_I("ProbeHandlerManager: attached {} probes", probes_.size());
    return true;
}

void ProbeHandlerManager::detach_all() {
    for (auto& entry : probes_) {
        if (entry.pad && entry.probe_id > 0) {
            gst_pad_remove_probe(entry.pad, entry.probe_id);
            LOG_D("ProbeHandlerManager: removed probe {} from '{}'", entry.probe_id,
                  entry.handler_id);
        }
        if (entry.pad) {
            gst_object_unref(entry.pad);
        }
    }
    probes_.clear();
    LOG_I("ProbeHandlerManager: all probes detached");
}

GstElement* ProbeHandlerManager::find_element(const std::string& name) const {
    if (!pipeline_)
        return nullptr;
    return gst_bin_get_by_name(GST_BIN(pipeline_), name.c_str());
}

}  // namespace engine::pipeline::probes
