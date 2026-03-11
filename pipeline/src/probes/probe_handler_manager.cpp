#include "engine/pipeline/probes/probe_handler_manager.hpp"
#include "engine/pipeline/probes/class_id_namespace_handler.hpp"
#include "engine/pipeline/probes/crop_object_handler.hpp"
#include "engine/pipeline/probes/frame_events_probe_handler.hpp"
#include "engine/pipeline/probes/smart_record_probe_handler.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline::probes {

ProbeHandlerManager::ProbeHandlerManager(GstElement* pipeline) : pipeline_(pipeline) {}

bool ProbeHandlerManager::attach_probes(const engine::core::config::PipelineConfig& config,
                                        engine::core::messaging::IMessageProducer* producer,
                                        engine::pipeline::evidence::FrameEvidenceCache* cache) {
    for (const auto& cfg : config.event_handlers) {
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

        const char* pad_name = cfg.pad_name.empty() ? "src" : cfg.pad_name.c_str();
        GstPad* pad = gst_element_get_static_pad(element, pad_name);
        if (!pad) {
            LOG_E("ProbeHandlerManager: no '{}' pad on '{}' for handler '{}'", pad_name,
                  cfg.probe_element, cfg.id);
            return false;
        }

        gulong probe_id = 0;

        // ── smart_record ──────────────────────────────────────────
        if (cfg.trigger == "smart_record") {
            // Resolve the configured source root. In manual mode the configured
            // element is the standalone nvstreammux, so SmartRecord needs its
            // parent bin to find sibling nvurisrcbin elements.
            GstElement* source_root = nullptr;
            const std::string default_source_root =
                config.sources.id.empty() ? std::string("sources_bin") : config.sources.id;
            const std::string configured_source_element =
                cfg.source_element.empty() ? default_source_root : cfg.source_element;
            if (!configured_source_element.empty()) {
                source_root = find_element(configured_source_element);
            }
            if (!source_root && configured_source_element != default_source_root) {
                LOG_W(
                    "ProbeHandlerManager: source_element '{}' not found for smart_record '{}' - "
                    "falling back to source root='{}'",
                    configured_source_element, cfg.id, default_source_root);
                source_root = find_element(default_source_root);
            }
            if (source_root && config.sources.type == "nvurisrcbin" && !GST_IS_BIN(source_root)) {
                GstObject* parent = gst_object_get_parent(GST_OBJECT(source_root));
                gst_object_unref(source_root);
                source_root = nullptr;

                if (parent && GST_IS_ELEMENT(parent)) {
                    source_root = GST_ELEMENT(parent);
                } else if (parent) {
                    gst_object_unref(parent);
                }
            }
            if (!source_root && config.sources.type == "nvurisrcbin" &&
                default_source_root != "sources_bin") {
                source_root = find_element("sources_bin");
            }
            if (!source_root) {
                LOG_E("ProbeHandlerManager: source_element '{}' not found for smart_record '{}'",
                      configured_source_element, cfg.id);
                gst_object_unref(pad);
                return false;
            }

            auto* handler = new SmartRecordProbeHandler();
            handler->configure(config, cfg, source_root, producer);
            probe_id = gst_pad_add_probe(
                pad, GST_PAD_PROBE_TYPE_BUFFER, SmartRecordProbeHandler::on_buffer, handler,
                [](gpointer ud) { delete static_cast<SmartRecordProbeHandler*>(ud); });
            gst_object_unref(source_root);

            // ── crop_objects ──────────────────────────────────────────
        } else if (cfg.trigger == "crop_objects") {
            auto* handler = new CropObjectHandler();
            handler->configure(config, cfg, producer);
            probe_id = gst_pad_add_probe(
                pad, GST_PAD_PROBE_TYPE_BUFFER, CropObjectHandler::on_buffer, handler,
                [](gpointer ud) { delete static_cast<CropObjectHandler*>(ud); });

            // ── frame_events ───────────────────────────────────────────
        } else if (cfg.trigger == "frame_events") {
            auto* handler = new FrameEventsProbeHandler();
            handler->configure(config, cfg, producer, cache);
            probe_id = gst_pad_add_probe(
                pad, GST_PAD_PROBE_TYPE_BUFFER, FrameEventsProbeHandler::on_buffer, handler,
                [](gpointer ud) { delete static_cast<FrameEventsProbeHandler*>(ud); });

            // ── class_id_offset ───────────────────────────────────────
        } else if (cfg.trigger == "class_id_offset") {
            auto* handler = new ClassIdNamespaceHandler();
            handler->configure(config, ClassIdNamespaceHandler::Mode::Offset);
            probe_id = gst_pad_add_probe(
                pad, GST_PAD_PROBE_TYPE_BUFFER, ClassIdNamespaceHandler::on_buffer, handler,
                [](gpointer ud) { delete static_cast<ClassIdNamespaceHandler*>(ud); });

            // ── class_id_restore ──────────────────────────────────────
        } else if (cfg.trigger == "class_id_restore") {
            auto* handler = new ClassIdNamespaceHandler();
            handler->configure(config, ClassIdNamespaceHandler::Mode::Restore);
            probe_id = gst_pad_add_probe(
                pad, GST_PAD_PROBE_TYPE_BUFFER, ClassIdNamespaceHandler::on_buffer, handler,
                [](gpointer ud) { delete static_cast<ClassIdNamespaceHandler*>(ud); });

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

    LOG_I("ProbeHandlerManager: attached {} probes total", probes_.size());
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

int ProbeHandlerManager::find_processing_element_index(
    const engine::core::config::PipelineConfig& config, const std::string& element_id) const {
    const auto& elems = config.processing.elements;
    for (int i = 0; i < static_cast<int>(elems.size()); ++i) {
        if (elems[i].id == element_id)
            return i;
    }
    LOG_W(
        "ProbeHandlerManager: processing element '{}' not found in config, "
        "using default index -1",
        element_id);
    return -1;
}

}  // namespace engine::pipeline::probes
