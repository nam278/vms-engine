#include "engine/pipeline/probes/class_id_namespace_handler.hpp"
#include "engine/core/utils/logger.hpp"

#include <gstnvdsmeta.h>

namespace engine::pipeline::probes {

void ClassIdNamespaceHandler::set_gie_unique_id(int gie_unique_id) {
    gie_unique_id_ = gie_unique_id;
    LOG_D("ClassIdNamespaceHandler: gie_unique_id={}", gie_unique_id_);
}

GstPadProbeReturn ClassIdNamespaceHandler::on_buffer(GstPad* /*pad*/, GstPadProbeInfo* info,
                                                     gpointer user_data) {
    auto* self = static_cast<ClassIdNamespaceHandler*>(user_data);

    GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
    NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buf);
    if (!batch_meta)
        return GST_PAD_PROBE_OK;

    for (NvDsMetaList* l_frame = batch_meta->frame_meta_list; l_frame; l_frame = l_frame->next) {
        auto* frame_meta = static_cast<NvDsFrameMeta*>(l_frame->data);

        for (NvDsMetaList* l_obj = frame_meta->obj_meta_list; l_obj; l_obj = l_obj->next) {
            auto* obj = static_cast<NvDsObjectMeta*>(l_obj->data);

            // Only remap objects from this specific GIE
            if (static_cast<int>(obj->unique_component_id) == self->gie_unique_id_) {
                // Namespace: gie_id * 1000 + original_class_id
                obj->class_id = static_cast<gint>(self->gie_unique_id_ * 1000 + obj->class_id);
            }
        }
    }

    return GST_PAD_PROBE_OK;
}

}  // namespace engine::pipeline::probes
