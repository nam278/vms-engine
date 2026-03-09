#include "engine/pipeline/evidence/frame_image_materializer.hpp"

#include "engine/pipeline/evidence/frame_evidence_cache.hpp"

#include <gst/gst.h>
#include <gstnvdsmeta.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace engine::pipeline::evidence {

namespace {

void populate_frame_meta(const CachedFrameEntry& entry, NvDsFrameMeta& frame_meta) {
    frame_meta.batch_id = 0;
    frame_meta.source_id = static_cast<guint>(entry.meta.source_id);
    frame_meta.frame_num = static_cast<gint>(entry.meta.frame_num);
    frame_meta.buf_pts = static_cast<guint64>(entry.meta.frame_ts_ms) * GST_MSECOND;
}

void populate_rect_object_meta(float left, float top, float width, float height, int class_id,
                               uint64_t object_id, NvDsObjectMeta& object_meta) {
    object_meta.object_id = static_cast<guint64>(object_id);
    object_meta.class_id = class_id;
    object_meta.rect_params.left = left;
    object_meta.rect_params.top = top;
    object_meta.rect_params.width = width;
    object_meta.rect_params.height = height;
}

bool encode_to_file(NvDsObjEncCtxHandle enc_ctx, NvBufSurface* surface, NvDsObjectMeta& object_meta,
                    NvDsFrameMeta& frame_meta, const std::string& file_path, int jpeg_quality,
                    bool is_frame) {
    NvDsObjEncUsrArgs args{};
    args.saveImg = TRUE;
    args.attachUsrMeta = FALSE;
    args.scaleImg = FALSE;
    args.quality = jpeg_quality;
    args.isFrame = is_frame ? TRUE : FALSE;
    std::snprintf(args.fileNameImg, sizeof(args.fileNameImg), "%s", file_path.c_str());

    if (!nvds_obj_enc_process(enc_ctx, &args, surface, &object_meta, &frame_meta)) {
        return false;
    }

    nvds_obj_enc_finish(enc_ctx);
    return true;
}

std::string unique_temp_file_path() {
    std::error_code ec;
    fs::path temp_dir = fs::temp_directory_path(ec);
    if (ec) {
        temp_dir = "/tmp";
    }

    std::ostringstream name;
    name << "vms_engine_extproc_"
         << std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count()
         << "_" << std::hash<std::thread::id>{}(std::this_thread::get_id()) << ".jpg";
    return (temp_dir / name.str()).string();
}

}  // namespace

bool FrameImageMaterializer::encode_overview_to_file(NvDsObjEncCtxHandle enc_ctx,
                                                     const CachedFrameEntry& entry,
                                                     const std::string& file_path, int jpeg_quality,
                                                     std::string& failure_reason) {
    if (!enc_ctx || !entry.surface) {
        failure_reason = "encoder_not_ready";
        return false;
    }

    NvDsFrameMeta frame_meta{};
    populate_frame_meta(entry, frame_meta);

    NvDsObjectMeta object_meta{};
    populate_rect_object_meta(0.0F, 0.0F, static_cast<float>(entry.meta.width),
                              static_cast<float>(entry.meta.height), 0, 0, object_meta);

    if (!encode_to_file(enc_ctx, entry.surface, object_meta, frame_meta, file_path, jpeg_quality,
                        true)) {
        failure_reason = "overview_encode_failed";
        return false;
    }

    return true;
}

bool FrameImageMaterializer::encode_crop_to_file(NvDsObjEncCtxHandle enc_ctx,
                                                 const CachedFrameEntry& entry, float left,
                                                 float top, float width, float height, int class_id,
                                                 uint64_t object_id, const std::string& file_path,
                                                 int jpeg_quality, std::string& failure_reason) {
    if (!enc_ctx || !entry.surface) {
        failure_reason = "encoder_not_ready";
        return false;
    }
    if (width <= 0.0F || height <= 0.0F) {
        failure_reason = "invalid_crop_bbox";
        return false;
    }

    NvDsFrameMeta frame_meta{};
    populate_frame_meta(entry, frame_meta);

    NvDsObjectMeta object_meta{};
    populate_rect_object_meta(left, top, width, height, class_id, object_id, object_meta);

    if (!encode_to_file(enc_ctx, entry.surface, object_meta, frame_meta, file_path, jpeg_quality,
                        false)) {
        failure_reason = "crop_encode_failed";
        return false;
    }

    return true;
}

bool FrameImageMaterializer::encode_crop_to_bytes(
    NvDsObjEncCtxHandle enc_ctx, const CachedFrameEntry& entry, float left, float top, float width,
    float height, int class_id, uint64_t object_id, int jpeg_quality,
    std::vector<unsigned char>& out_bytes, std::string& failure_reason) {
    const std::string temp_path = unique_temp_file_path();
    if (!encode_crop_to_file(enc_ctx, entry, left, top, width, height, class_id, object_id,
                             temp_path, jpeg_quality, failure_reason)) {
        return false;
    }

    std::ifstream input(temp_path, std::ios::binary);
    if (!input) {
        failure_reason = "read_temp_crop_failed";
        std::error_code remove_ec;
        fs::remove(temp_path, remove_ec);
        return false;
    }

    out_bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    input.close();

    std::error_code remove_ec;
    fs::remove(temp_path, remove_ec);
    if (out_bytes.empty()) {
        failure_reason = "empty_temp_crop";
        return false;
    }

    return true;
}

}  // namespace engine::pipeline::evidence