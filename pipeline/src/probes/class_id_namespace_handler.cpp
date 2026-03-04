#include "engine/pipeline/probes/class_id_namespace_handler.hpp"
#include "engine/core/utils/logger.hpp"

#include <gstnvdsmeta.h>

namespace engine::pipeline::probes {

// ── Configure ──────────────────────────────────────────────────────

void ClassIdNamespaceHandler::configure(const engine::core::config::PipelineConfig& /*config*/,
                                        Mode mode, int base_no_offset, int offset_step) {
    mode_ = mode;
    base_no_offset_ = base_no_offset;
    offset_step_ = offset_step;

    if (mode_ == Mode::Offset) {
        LOG_I(
            "ClassIdNamespaceHandler [Offset]: base_no_offset={}, offset_step={} "
            "(GIEs with uid>{} will be remapped)",
            base_no_offset_, offset_step_, base_no_offset_);
    } else {
        LOG_I("ClassIdNamespaceHandler [Restore]: will restore original class_ids");
    }
}

void ClassIdNamespaceHandler::set_explicit_offsets(const std::unordered_map<int, int>& offsets) {
    explicit_offsets_ = offsets;
}

// ── Offset Computation ─────────────────────────────────────────────

int ClassIdNamespaceHandler::compute_offset(int gie_unique_id) const {
    // Explicit overrides take priority
    auto it = explicit_offsets_.find(gie_unique_id);
    if (it != explicit_offsets_.end()) {
        return it->second;
    }
    // GIEs with uid <= base_no_offset_ (e.g. PGIE=1) are not offset
    if (gie_unique_id <= base_no_offset_) {
        return 0;
    }
    return (gie_unique_id - base_no_offset_) * offset_step_;
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

            // Idempotent: skip if already marked
            if (obj->misc_obj_info[0] == static_cast<gint64>(MAGIC_MARKER)) {
                continue;
            }

            // Compute offset for this object's GIE
            const int uid = static_cast<int>(obj->unique_component_id);
            const int offset = compute_offset(uid);

            // Store originals in misc_obj_info before any modification
            obj->misc_obj_info[0] = static_cast<gint64>(MAGIC_MARKER);
            obj->misc_obj_info[1] = static_cast<gint64>(obj->class_id);
            obj->misc_obj_info[2] = static_cast<gint64>(uid);

            // Apply namespace offset (0 for PGIE: no-op but still marks for restore symmetry)
            if (offset > 0) {
                obj->class_id = static_cast<gint>(offset + obj->class_id);
            }
        }
    }

    return GST_PAD_PROBE_OK;
}

// ── Restore Mode ───────────────────────────────────────────────────

GstPadProbeReturn ClassIdNamespaceHandler::process_restore(GstBuffer* buf) {
    NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buf);
    if (!batch_meta)
        return GST_PAD_PROBE_OK;

    int restored_by_marker = 0;
    int restored_by_fallback = 0;

    for (NvDsMetaList* l_frame = batch_meta->frame_meta_list; l_frame; l_frame = l_frame->next) {
        auto* frame_meta = static_cast<NvDsFrameMeta*>(l_frame->data);

        for (NvDsMetaList* l_obj = frame_meta->obj_meta_list; l_obj; l_obj = l_obj->next) {
            auto* obj = static_cast<NvDsObjectMeta*>(l_obj->data);

            // Only restore if magic marker is present (was previously offset)
            if (obj->misc_obj_info[0] == static_cast<gint64>(MAGIC_MARKER)) {
                // Restore originals
                obj->class_id = static_cast<gint>(obj->misc_obj_info[1]);
                obj->unique_component_id = static_cast<gint>(obj->misc_obj_info[2]);

                // Clear marker
                obj->misc_obj_info[0] = 0;
                obj->misc_obj_info[1] = 0;
                obj->misc_obj_info[2] = 0;
                restored_by_marker++;
                continue;
            }

            // Fallback restore path:
            // Some DeepStream elements (notably tracker/meta copy flows) may drop
            // misc_obj_info marker fields. In that case, derive offset from the
            // current unique_component_id and de-namespace class_id heuristically.
            const int uid = static_cast<int>(obj->unique_component_id);
            const int offset = compute_offset(uid);
            if (offset > 0 && obj->class_id >= offset) {
                obj->class_id = static_cast<gint>(obj->class_id - offset);
                restored_by_fallback++;
            }
        }
    }

    if (restored_by_fallback > 0) {
        LOG_T(
            "ClassIdNamespaceHandler [Restore]: restored by marker={}, by fallback={} "
            "(marker lost on some objects)",
            restored_by_marker, restored_by_fallback);
    }

    return GST_PAD_PROBE_OK;
}

}  // namespace engine::pipeline::probes
