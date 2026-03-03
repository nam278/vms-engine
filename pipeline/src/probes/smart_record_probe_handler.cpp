#include "engine/pipeline/probes/smart_record_probe_handler.hpp"
#include "engine/core/utils/logger.hpp"

#include <gstnvdsmeta.h>
#include <algorithm>

namespace engine::pipeline::probes {

void SmartRecordProbeHandler::configure(const engine::core::config::EventHandlerConfig& config) {
    label_filter_ = config.label_filter;
    pre_event_sec_ = config.pre_event_sec;
    post_event_sec_ = config.post_event_sec;
    save_dir_ = config.save_dir;

    LOG_D(
        "SmartRecordProbeHandler: configured with {} labels, "
        "pre={}s post={}s dir='{}'",
        label_filter_.size(), pre_event_sec_, post_event_sec_, save_dir_);
}

GstPadProbeReturn SmartRecordProbeHandler::on_buffer(GstPad* /*pad*/, GstPadProbeInfo* info,
                                                     gpointer user_data) {
    auto* self = static_cast<SmartRecordProbeHandler*>(user_data);

    GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
    NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buf);
    if (!batch_meta)
        return GST_PAD_PROBE_OK;

    for (NvDsMetaList* l_frame = batch_meta->frame_meta_list; l_frame; l_frame = l_frame->next) {
        auto* frame_meta = static_cast<NvDsFrameMeta*>(l_frame->data);

        for (NvDsMetaList* l_obj = frame_meta->obj_meta_list; l_obj; l_obj = l_obj->next) {
            auto* obj = static_cast<NvDsObjectMeta*>(l_obj->data);

            // Check if this object's label matches the filter
            const std::string label(obj->obj_label);
            if (!self->label_filter_.empty()) {
                auto it = std::find(self->label_filter_.begin(), self->label_filter_.end(), label);
                if (it == self->label_filter_.end()) {
                    continue;  // Label not in filter
                }
            }

            // TODO: Trigger smart recording via NvDsSRStart() on source bin
            // NvDsSRStart(src_bin, source_id,
            //     frame_meta->source_id,
            //     self->pre_event_sec_,
            //     self->post_event_sec_);
            LOG_D("SmartRecordProbe: match label='{}' source={} frame={}", label,
                  frame_meta->source_id, frame_meta->frame_num);
        }
    }

    return GST_PAD_PROBE_OK;
}

}  // namespace engine::pipeline::probes
