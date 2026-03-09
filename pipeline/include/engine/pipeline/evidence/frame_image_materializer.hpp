#pragma once

#include <cstdint>
#include <gstnvdsmeta.h>
#include <nvbufsurface.h>
#include <nvds_obj_encode.h>
#include <string>
#include <vector>

namespace engine::pipeline::evidence {

struct CachedFrameEntry;

/** @brief Shared JPEG materialization helpers built on top of cached emitted frames. */
class FrameImageMaterializer {
   public:
    static bool encode_overview_to_file(NvDsObjEncCtxHandle enc_ctx, const CachedFrameEntry& entry,
                                        const std::string& file_path, int jpeg_quality,
                                        std::string& failure_reason);

    static bool encode_crop_to_file(NvDsObjEncCtxHandle enc_ctx, const CachedFrameEntry& entry,
                                    float left, float top, float width, float height, int class_id,
                                    uint64_t object_id, const std::string& file_path,
                                    int jpeg_quality, std::string& failure_reason);

    static bool encode_crop_to_bytes(NvDsObjEncCtxHandle enc_ctx, const CachedFrameEntry& entry,
                                     float left, float top, float width, float height, int class_id,
                                     uint64_t object_id, int jpeg_quality,
                                     std::vector<unsigned char>& out_bytes,
                                     std::string& failure_reason);
};

}  // namespace engine::pipeline::evidence