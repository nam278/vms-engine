#include <map>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
#include <string>
#include <cuda_fp16.h>
#include "NvInfer.h"

#include "nvdsinfer_custom_impl.h"

extern "C" bool NvDsInferParseYolo(std::vector<NvDsInferLayerInfo> const& outputLayersInfo,
                                   NvDsInferNetworkInfo const& networkInfo,
                                   NvDsInferParseDetectionParams const& detectionParams,
                                   std::vector<NvDsInferParseObjectInfo>& objectList);

static float clamp(const float val, const float minVal, const float maxVal) {
    assert(minVal <= maxVal);
    return std::min(maxVal, std::max(minVal, val));
}

static NvDsInferParseObjectInfo convertBBox(const float& bx1, const float& by1, const float& bx2,
                                            const float& by2, const uint& netW, const uint& netH) {
    NvDsInferParseObjectInfo b;

    float x1 = bx1;
    float y1 = by1;
    float x2 = bx2;
    float y2 = by2;

    x1 = clamp(x1, 0, netW);
    y1 = clamp(y1, 0, netH);
    x2 = clamp(x2, 0, netW);
    y2 = clamp(y2, 0, netH);

    b.left = x1;
    b.width = clamp(x2 - x1, 0, netW);
    b.top = y1;
    b.height = clamp(y2 - y1, 0, netH);

    return b;
}

static void addBBoxProposal(const float bx1, const float by1, const float bx2, const float by2,
                            const uint& netW, const uint& netH, const int maxIndex,
                            const float maxProb, std::vector<NvDsInferParseObjectInfo>& binfo) {
    NvDsInferParseObjectInfo bbi = convertBBox(bx1, by1, bx2, by2, netW, netH);

    if (bbi.width < 1 || bbi.height < 1) {
        return;
    }

    bbi.detectionConfidence = maxProb;
    bbi.classId = maxIndex;
    binfo.push_back(bbi);
}

static std::vector<NvDsInferParseObjectInfo> decodeTensorYolo(
    const float* output, const uint& outputSize, const uint& netW, const uint& netH,
    const std::vector<float>& preclusterThreshold) {
    std::vector<NvDsInferParseObjectInfo> binfo;

    for (uint b = 0; b < outputSize; ++b) {
        float maxProb = output[b * 6 + 4];
        int maxIndex = (int)output[b * 6 + 5];

        if (maxProb < preclusterThreshold[maxIndex]) {
            continue;
        }

        float bx1 = output[b * 6 + 0];
        float by1 = output[b * 6 + 1];
        float bx2 = output[b * 6 + 2];
        float by2 = output[b * 6 + 3];

        addBBoxProposal(bx1, by1, bx2, by2, netW, netH, maxIndex, maxProb, binfo);
    }

    return binfo;
}

static bool NvDsInferParseCustomYolo(std::vector<NvDsInferLayerInfo> const& outputLayersInfo,
                                     NvDsInferNetworkInfo const& networkInfo,
                                     NvDsInferParseDetectionParams const& detectionParams,
                                     std::vector<NvDsInferParseObjectInfo>& objectList) {
    if (outputLayersInfo.empty()) {
        std::cerr << "ERROR: Could not find output layer in bbox parsing" << std::endl;
        return false;
    }

    std::vector<NvDsInferParseObjectInfo> objects;

    const NvDsInferLayerInfo& output = outputLayersInfo[0];
    const uint outputSize = output.inferDims.d[0];

    std::vector<NvDsInferParseObjectInfo> outObjs =
        decodeTensorYolo((const float*)(output.buffer), outputSize, networkInfo.width,
                         networkInfo.height, detectionParams.perClassPreclusterThreshold);

    objects.insert(objects.end(), outObjs.begin(), outObjs.end());

    objectList = objects;

    return true;
}

extern "C" bool NvDsInferParseYolo(std::vector<NvDsInferLayerInfo> const& outputLayersInfo,
                                   NvDsInferNetworkInfo const& networkInfo,
                                   NvDsInferParseDetectionParams const& detectionParams,
                                   std::vector<NvDsInferParseObjectInfo>& objectList) {
    return NvDsInferParseCustomYolo(outputLayersInfo, networkInfo, detectionParams, objectList);
}

CHECK_CUSTOM_PARSE_FUNC_PROTOTYPE(NvDsInferParseYolo);

// # x86_64 (thường đủ)
// g++ -O2 -std=c++17 -fPIC -shared -o libparser_yolo_debug.so parser_yolo_debug.cpp \
//   -I/opt/nvidia/deepstream/deepstream/sources/includes \
//   -I/usr/local/cuda/include

// # Jetson nếu symlink /usr/local/cuda không có:
// g++ -O2 -std=c++17 -fPIC -shared -o libparser_yolo_debug.so parser_yolo_debug.cpp \
//   -I/opt/nvidia/deepstream/deepstream/sources/includes \
//   -I/usr/local/cuda/targets/aarch64-linux/include
