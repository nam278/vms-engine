// plugins/src/libnvdsinfer_custom_impl_Yolo_padded.cpp
// Custom YOLO parser with bbox padding for NVIDIA DeepStream

#include <map>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>  // std::min, std::max

#include <cuda_fp16.h>
#include "NvInfer.h"
#include "nvdsinfer_custom_impl.h"

// ──────────────────────────────────────────────────────────────
// CONFIG: chọn 1 trong 3 kiểu padding (bật đúng 1 cái)
// ──────────────────────────────────────────────────────────────
// #define PAD_PIXELS  // A) padding tuyệt đối theo pixel (kPadPx)
// #define PAD_PERCENT_FRAME    // B) padding theo % kích thước khung mạng (kPadPct * max(netW,
// netH))
#define PAD_PERCENT_BOX  // C) padding theo % kích thước chính bbox (kPadPctBox * max(w, h))

// A) Pixel
static constexpr float kPadPx = 30.0f;

// B) % theo frame (0.06 = 6%)
static constexpr float kPadPct = 0.06f;

// C) % theo chính bbox (0.10 = 10%)
static constexpr float kPadPctBox = 0.15f;

// Nếu output đang ở toạ độ chuẩn hoá [0,1], bật macro này để scale về netW/netH trước khi padding
// #define BBOX_COORDS_NORMALIZED

extern "C" bool NvDsInferParseYolo(std::vector<NvDsInferLayerInfo> const& outputLayersInfo,
                                   NvDsInferNetworkInfo const& networkInfo,
                                   NvDsInferParseDetectionParams const& detectionParams,
                                   std::vector<NvDsInferParseObjectInfo>& objectList);

static inline float clampf(const float val, const float minVal, const float maxVal) {
    assert(minVal <= maxVal);
    return std::min(maxVal, std::max(minVal, val));
}

static NvDsInferParseObjectInfo makeBboxPadded(float x1, float y1, float x2, float y2,
                                               const uint netW, const uint netH) {
    // Chuyển về pixel nếu đầu vào đang normalized
#ifdef BBOX_COORDS_NORMALIZED
    x1 *= netW;
    x2 *= netW;
    y1 *= netH;
    y2 *= netH;
#endif

    // Clamp input ngay biên
    x1 = clampf(x1, 0.f, (float)netW);
    y1 = clampf(y1, 0.f, (float)netH);
    x2 = clampf(x2, 0.f, (float)netW);
    y2 = clampf(y2, 0.f, (float)netH);

    float w = std::max(0.f, x2 - x1);
    float h = std::max(0.f, y2 - y1);

    // Tính padding
#if defined(PAD_PIXELS)
    float pad = kPadPx;
#elif defined(PAD_PERCENT_FRAME)
    float pad = kPadPct * std::max(netW, netH);
#elif defined(PAD_PERCENT_BOX)
    float pad = kPadPctBox * std::max(w, h);
#else
    float pad = 0.f;
#endif

    // Nới bbox + clamp lại
    float nx1 = clampf(x1 - pad, 0.f, (float)netW);
    float ny1 = clampf(y1 - pad, 0.f, (float)netH);
    float nx2 = clampf(x2 + pad, 0.f, (float)netW);
    float ny2 = clampf(y2 + pad, 0.f, (float)netH);

    NvDsInferParseObjectInfo b{};
    b.left = nx1;
    b.top = ny1;
    b.width = std::max(0.f, nx2 - nx1);
    b.height = std::max(0.f, ny2 - ny1);
    return b;
}

static void addBBoxProposal(const float bx1, const float by1, const float bx2, const float by2,
                            const uint netW, const uint netH, const int classId, const float conf,
                            std::vector<NvDsInferParseObjectInfo>& out) {
    NvDsInferParseObjectInfo bb = makeBboxPadded(bx1, by1, bx2, by2, netW, netH);
    if (bb.width < 1.f || bb.height < 1.f)
        return;
    bb.classId = classId;
    bb.detectionConfidence = conf;
    out.push_back(bb);
}

// Giả định output là danh sách detection dạng [x1,y1,x2,y2,score,classId] độ dài = outputSize*6.
// Điều chỉnh nếu mô hình của bạn có layout khác.
static std::vector<NvDsInferParseObjectInfo> decodeTensorYolo(
    const float* output, const uint outputSize, const uint netW, const uint netH,
    const std::vector<float>& preclusterThreshold) {
    std::vector<NvDsInferParseObjectInfo> boxes;
    boxes.reserve(outputSize);

    for (uint i = 0; i < outputSize; ++i) {
        const float* p = output + i * 6;
        float x1 = p[0], y1 = p[1], x2 = p[2], y2 = p[3];
        float score = p[4];
        int cls = static_cast<int>(p[5]);

        // Bảo vệ index
        if (cls < 0 || cls >= (int)preclusterThreshold.size())
            continue;
        if (score < preclusterThreshold[cls])
            continue;

        addBBoxProposal(x1, y1, x2, y2, netW, netH, cls, score, boxes);
    }
    return boxes;
}

static bool NvDsInferParseCustomYolo(std::vector<NvDsInferLayerInfo> const& outputLayersInfo,
                                     NvDsInferNetworkInfo const& networkInfo,
                                     NvDsInferParseDetectionParams const& detectionParams,
                                     std::vector<NvDsInferParseObjectInfo>& objectList) {
    if (outputLayersInfo.empty()) {
        std::cerr << "ERROR: No output layers found for YOLO parser.\n";
        return false;
    }

    // Lấy tensor đầu tiên (điều chỉnh nếu mô hình có nhiều head)
    const NvDsInferLayerInfo& out0 = outputLayersInfo[0];

    // Kiểm tra type
    if (out0.dataType != NvDsInferDataType::FLOAT) {
        std::cerr << "ERROR: YOLO parser expects FLOAT output tensor.\n";
        return false;
    }

    // Suy ra số detection từ dims. Ở ví dụ này giả định [N,6] hoặc [D] với D%6==0.
    uint outputSize = 0;
    if (out0.inferDims.numDims == 2 && out0.inferDims.d[1] == 6) {
        outputSize = static_cast<uint>(out0.inferDims.d[0]);
    } else {
        // Fallback: tính từ kích cỡ buffer/6
        // Lưu ý: stride = sizeof(float)
        // DeepStream không cung cấp trực tiếp số phần tử, nên ta dùng dims để suy luận
        // hoặc bạn có thể truyền kèm theo qua custom config nếu layout khác.
        int numElems = 1;
        for (int k = 0; k < out0.inferDims.numDims; ++k)
            numElems *= out0.inferDims.d[k];
        outputSize = (numElems >= 6) ? (uint)(numElems / 6) : 0u;
    }

    if (outputSize == 0) {
        // Không có detection
        objectList.clear();
        return true;
    }

    const float* data = static_cast<const float*>(out0.buffer);
    auto decoded = decodeTensorYolo(data, outputSize, networkInfo.width, networkInfo.height,
                                    detectionParams.perClassPreclusterThreshold);

    objectList.swap(decoded);
    return true;
}

extern "C" bool NvDsInferParseYolo(std::vector<NvDsInferLayerInfo> const& outputLayersInfo,
                                   NvDsInferNetworkInfo const& networkInfo,
                                   NvDsInferParseDetectionParams const& detectionParams,
                                   std::vector<NvDsInferParseObjectInfo>& objectList) {
    return NvDsInferParseCustomYolo(outputLayersInfo, networkInfo, detectionParams, objectList);
}

// Macro NVIDIA: kiểm tra prototype hàm parser tại compile-time
CHECK_CUSTOM_PARSE_FUNC_PROTOTYPE(NvDsInferParseYolo);
