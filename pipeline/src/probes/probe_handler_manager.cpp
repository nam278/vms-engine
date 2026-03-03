#include "engine/pipeline/probes/probe_handler_manager.hpp"
#include "engine/pipeline/probes/crop_object_handler.hpp"
#include "engine/pipeline/probes/smart_record_probe_handler.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::probes {

ProbeHandlerManager::ProbeHandlerManager(GstElement* pipeline) : pipeline_(pipeline) {}

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

        gulong probe_id = 0;

        if (cfg.trigger == "smart_record") {
            auto* handler = new SmartRecordProbeHandler();
            handler->configure(cfg);
            probe_id = gst_pad_add_probe(
                pad, GST_PAD_PROBE_TYPE_BUFFER, SmartRecordProbeHandler::on_buffer, handler,
                [](gpointer ud) { delete static_cast<SmartRecordProbeHandler*>(ud); });

        } else if (cfg.trigger == "crop_objects") {
            auto* handler = new CropObjectHandler();
            handler->configure(cfg);
            probe_id = gst_pad_add_probe(
                pad, GST_PAD_PROBE_TYPE_BUFFER, CropObjectHandler::on_buffer, handler,
                [](gpointer ud) { delete static_cast<CropObjectHandler*>(ud); });

        } else {
            LOG_W("ProbeHandlerManager: unknown trigger '{}' for handler '{}', skipping",
                  cfg.trigger, cfg.id);
            gst_object_unref(pad);
            continue;
        }

        probes_.push_back({element, pad, probe_id, cfg.id});
        LOG_I("ProbeHandlerManager: attached '{}' probe on '{}' for handler '{}'", cfg.trigger,
              cfg.probe_element, cfg.id);
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
