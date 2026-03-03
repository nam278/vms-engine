#include "engine/pipeline/probes/crop_object_handler.hpp"
#include "engine/core/utils/logger.hpp"

#include <gstnvdsmeta.h>
#include <algorithm>
#include <chrono>

namespace engine::pipeline::probes {

void CropObjectHandler::configure(const engine::core::config::EventHandlerConfig& config) {
    label_filter_ = config.label_filter;
    save_dir_ = config.save_dir;
    capture_interval_sec_ = config.capture_interval_sec;
    image_quality_ = config.image_quality;
    save_full_frame_ = config.save_full_frame;

    LOG_D(
        "CropObjectHandler: configured with {} labels, interval={}s, "
        "quality={}, dir='{}'",
        label_filter_.size(), capture_interval_sec_, image_quality_, save_dir_);
}

GstPadProbeReturn CropObjectHandler::on_buffer(GstPad* /*pad*/, GstPadProbeInfo* info,
                                               gpointer user_data) {
    auto* self = static_cast<CropObjectHandler*>(user_data);

    GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
    NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buf);
    if (!batch_meta)
        return GST_PAD_PROBE_OK;

    auto now = std::chrono::steady_clock::now().time_since_epoch();
    auto now_sec = std::chrono::duration_cast<std::chrono::seconds>(now).count();

    for (NvDsMetaList* l_frame = batch_meta->frame_meta_list; l_frame; l_frame = l_frame->next) {
        auto* frame_meta = static_cast<NvDsFrameMeta*>(l_frame->data);
        int src_id = static_cast<int>(frame_meta->source_id);

        // Throttle captures per source
        auto it = self->last_capture_time_.find(src_id);
        if (it != self->last_capture_time_.end()) {
            if (now_sec - it->second < self->capture_interval_sec_) {
                continue;  // Too soon since last capture
            }
        }

        bool captured = false;
        for (NvDsMetaList* l_obj = frame_meta->obj_meta_list; l_obj; l_obj = l_obj->next) {
            auto* obj = static_cast<NvDsObjectMeta*>(l_obj->data);
            const std::string label(obj->obj_label);

            // Label filter check
            if (!self->label_filter_.empty()) {
                auto fit = std::find(self->label_filter_.begin(), self->label_filter_.end(), label);
                if (fit == self->label_filter_.end())
                    continue;
            }

            // TODO: Perform actual crop using NvBufSurface API
            // 1. Get NvBufSurface from frame_meta->buf_surface_params
            // 2. Call crop_and_save_object() from object_crop.hpp
            // 3. Save to: save_dir_/src_{src_id}/frame_{frame_num}_{obj_id}.jpg

            LOG_D(
                "CropObjectHandler: would crop label='{}' src={} "
                "bbox=({:.0f},{:.0f},{:.0f},{:.0f})",
                label, src_id, obj->rect_params.left, obj->rect_params.top, obj->rect_params.width,
                obj->rect_params.height);

            captured = true;
        }

        if (captured) {
            self->last_capture_time_[src_id] = now_sec;
        }
    }

    return GST_PAD_PROBE_OK;
}

}  // namespace engine::pipeline::probes
