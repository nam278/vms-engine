#pragma once

#include <string>

// Forward declarations — avoid pulling heavy DeepStream headers
struct NvBufSurface;

namespace engine::pipeline::event_handlers {

/**
 * @brief Utility for cropping objects from NvBufSurface GPU buffers.
 *
 * Used by CropDetectedObjHandler and pad probes that need to extract
 * object regions for saving as JPEG images.
 *
 * All functions are free-standing helpers (no class state needed).
 */

/**
 * @brief Crop a rectangular region from an NvBufSurface and encode to JPEG.
 *
 * @param surface   GPU surface containing frame data.
 * @param batch_id  Index of the frame within the batch.
 * @param left      Bounding box left offset in pixels.
 * @param top       Bounding box top offset in pixels.
 * @param width     Bounding box width in pixels.
 * @param height    Bounding box height in pixels.
 * @param output_path  File path to write the JPEG image.
 * @param quality   JPEG quality (1-100, default 85).
 * @return true if crop and save succeeded.
 */
inline bool crop_and_save_object(NvBufSurface* surface, int batch_id, float left, float top,
                                 float width, float height, const std::string& output_path,
                                 int quality = 85) {
    // TODO: Full implementation requires:
    // 1. NvBufSurfaceMap(surface, batch_id, -1, NVBUF_MAP_READ)
    // 2. Extract ROI from mapped pointer using (left, top, width, height)
    // 3. Convert NV12/RGBA to BGR for JPEG encoding
    // 4. Write JPEG using libjpeg/nvjpeg/stb_image_write
    // 5. NvBufSurfaceUnMap(surface, batch_id, -1)
    //
    // Placeholder — returns false until DeepStream surface API is integrated
    (void)surface;
    (void)batch_id;
    (void)left;
    (void)top;
    (void)width;
    (void)height;
    (void)output_path;
    (void)quality;
    return false;
}

}  // namespace engine::pipeline::event_handlers
