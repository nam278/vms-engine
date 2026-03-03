#include "engine/pipeline/probes/class_id_namespace_handler.hpp"
#include "engine/core/utils/logger.hpp"

#include <gstnvdsmeta.h>

namespace engine::pipeline::probes {

// ── Configure ──────────────────────────────────────────────────────

void ClassIdNamespaceHandler::configure(const engine::core::config::PipelineConfig& config,
                                        Mode mode, int element_index) {
    mode_ = mode;

    if (mode_ == Mode::Offset && element_index >= 0 &&
        element_index < static_cast<int>(config.processing.elements.size())) {
        const auto& elem = config.processing.elements[element_index];
        gie_unique_id_ = elem.unique_id;
        base_offset_ = compute_offset(gie_unique_id_);
        LOG_I("ClassIdNamespaceHandler [Offset]: gie_unique_id={}, base_offset={}", gie_unique_id_,
              base_offset_);
    } else if (mode_ == Mode::Restore) {
        LOG_I("ClassIdNamespaceHandler [Restore]: will restore original class_ids");
    } else if (mode_ == Mode::Offset) {
        LOG_W("ClassIdNamespaceHandler [Offset]: no valid element_index={}, default offset={}",
              element_index, base_offset_);
    }
}

void ClassIdNamespaceHandler::set_explicit_offsets(const std::unordered_map<int, int>& offsets) {
    explicit_offsets_ = offsets;
    // Recompute base_offset in case our own GIE is in the explicit map
    base_offset_ = compute_offset(gie_unique_id_);
}

// ── Offset Computation ─────────────────────────────────────────────

int ClassIdNamespaceHandler::compute_offset(int gie_unique_id) const {
    auto it = explicit_offsets_.find(gie_unique_id);
    if (it != explicit_offsets_.end()) {
        return it->second;
    }
    return gie_unique_id * offset_step_;
}

// ── Static Probe Callback ──────────────────────────────────────────

GstPadProbeReturn ClassIdNamespaceHandler::on_buffer(GstPad* /*pad*/, GstPadProbeInfo* info,
                                                     gpointer user_data) {
    auto* self = static_cast<ClassIdNamespaceHandler*>(user_data);
    GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!buf)
        return GST_PAD_PROBE_OK;

    if (self->mode_ == Mode::Offset) {
        return self->process_offset(buf);
    }
    return self->process_restore(buf);
}

// ── Offset Mode ────────────────────────────────────────────────────

GstPadProbeReturn ClassIdNamespaceHandler::process_offset(GstBuffer* buf) {
    NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buf);
    if (!batch_meta)
        return GST_PAD_PROBE_OK;

    for (NvDsMetaList* l_frame = batch_meta->frame_meta_list; l_frame; l_frame = l_frame->next) {
        auto* frame_meta = static_cast<NvDsFrameMeta*>(l_frame->data);

        for (NvDsMetaList* l_obj = frame_meta->obj_meta_list; l_obj; l_obj = l_obj->next) {
            auto* obj = static_cast<NvDsObjectMeta*>(l_obj->data);

            // Only remap objects from this specific GIE
            if (static_cast<int>(obj->unique_component_id) != gie_unique_id_) {
                continue;
            }

            // Idempotent: skip if already marked
            if (obj->misc_obj_info[0] == static_cast<gint64>(MAGIC_MARKER)) {
                continue;
            }

            // Store originals in misc_obj_info before modifying
            obj->misc_obj_info[0] = static_cast<gint64>(MAGIC_MARKER);
            obj->misc_obj_info[1] = static_cast<gint64>(obj->class_id);
            obj->misc_obj_info[2] = static_cast<gint64>(obj->unique_component_id);

            // Apply namespace offset
            obj->class_id = static_cast<gint>(base_offset_ + obj->class_id);
        }
    }

    return GST_PAD_PROBE_OK;
}

// ── Restore Mode ───────────────────────────────────────────────────

GstPadProbeReturn ClassIdNamespaceHandler::process_restore(GstBuffer* buf) {
    NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buf);
    if (!batch_meta)
        return GST_PAD_PROBE_OK;

    for (NvDsMetaList* l_frame = batch_meta->frame_meta_list; l_frame; l_frame = l_frame->next) {
        auto* frame_meta = static_cast<NvDsFrameMeta*>(l_frame->data);

        for (NvDsMetaList* l_obj = frame_meta->obj_meta_list; l_obj; l_obj = l_obj->next) {
            auto* obj = static_cast<NvDsObjectMeta*>(l_obj->data);

            // Only restore if magic marker is present (was previously offset)
            if (obj->misc_obj_info[0] != static_cast<gint64>(MAGIC_MARKER)) {
                continue;
            }

            // Restore originals
            obj->class_id = static_cast<gint>(obj->misc_obj_info[1]);
            obj->unique_component_id = static_cast<gint>(obj->misc_obj_info[2]);

            // Clear marker
            obj->misc_obj_info[0] = 0;
            obj->misc_obj_info[1] = 0;
            obj->misc_obj_info[2] = 0;
        }
    }

    return GST_PAD_PROBE_OK;
}

}  // namespace engine::pipeline::probes
