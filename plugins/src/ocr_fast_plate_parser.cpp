// plugins/src/ocr_fast_plate_parser.cpp
//
// Custom parser cho OCR biển số kiểu multi-head classification (fast-plate-ocr).
// Theo Plate Config mặc định: max_plate_slots = 9, alphabet =
// "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_" (ký tự '_' là pad, sẽ bỏ khi ghép chuỗi kết quả).
//
// Dùng với DeepStream 7 (nvinferserver):
//   postprocess { classification { custom_parse_classifier_func:
//   "NvDsInferClassifierParseCustomFastPlateOCR" } } custom_lib { path:
//   "/opt/lantana/lib/libocr_fast_plate_parser.so" }

#include <cstring>
#include <iostream>
#include <vector>
#include <string>
#include <cuda_fp16.h>
#include "nvdsinfer_custom_impl.h"

// --- cấu hình alphabet/slots theo fast-plate-ocr ---
static const char* kAlphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_";
static inline int kClasses() {
    return 37;
}  // 36 ký tự + '_' (pad)
static inline int kSlots() {
    return 9;
}  // max_plate_slots

extern "C" bool NvDsInferClassifierParseCustomFastPlateOCR(
    const std::vector<NvDsInferLayerInfo>& outLayers, const NvDsInferNetworkInfo& networkInfo,
    float classifierThreshold, std::vector<NvDsInferAttribute>& attrList, std::string& descString) {
    if (outLayers.size() != 1) {
        std::cerr << "[OCR] Expect exactly 1 output layer (S x C)\n";
        return false;
    }
    const NvDsInferLayerInfo& L = outLayers[0];

    // Suy ra (S, C) từ inferDims
    int d[8] = {0};
    for (unsigned i = 0; i < L.inferDims.numDims; ++i)
        d[i] = L.inferDims.d[i];

    int S = 0, C = 0;  // slots, classes
    if (L.inferDims.numDims == 2) {
        S = d[0];
        C = d[1];
    } else if (L.inferDims.numDims == 3) {
        // giả định [N, S, C] với N=1
        S = d[1];
        C = d[2];
    } else if (L.inferDims.numDims == 1) {
        // flatten: [S*C]
        C = kClasses();
        int total = d[0];
        if (C > 0 && total % C == 0)
            S = total / C;
    }

    if (S <= 0 || C <= 0) {
        std::cerr << "[OCR] Bad output dims\n";
        return false;
    }
    if (C != kClasses()) {
        std::cerr << "[OCR] Class mismatch: model C=" << C << " vs expected " << kClasses()
                  << " — update kAlphabet/kClasses if your Plate Config changed.\n";
        // vẫn tiếp tục parse để log cho dễ debug
    }

    // đọc buffer về float theo layout [S, C]
    std::vector<float> logits;
    logits.resize(static_cast<size_t>(S) * static_cast<size_t>(C));

    if (L.dataType == NvDsInferDataType::FLOAT) {
        const float* p = static_cast<const float*>(L.buffer);
        // copy bằng vòng for để không cần <algorithm>
        const int N = S * C;
        for (int i = 0; i < N; ++i)
            logits[i] = p[i];
    } else if (L.dataType == NvDsInferDataType::HALF) {
        const __half* p = static_cast<const __half*>(L.buffer);
        const int N = S * C;
        for (int i = 0; i < N; ++i)
            logits[i] = __half2float(p[i]);
    } else {
        std::cerr << "[OCR] Unsupported dtype (expect FP32/FP16)\n";
        return false;
    }

    // Argmax từng slot, bỏ ký tự '_' khi ghép
    std::string plate;
    plate.reserve(S);
    for (int s = 0; s < S; ++s) {
        const float* row = &logits[s * C];

        int best_k = 0;
        float best_v = row[0];
        for (int k = 1; k < C; ++k) {
            float v = row[k];
            if (v > best_v) {
                best_v = v;
                best_k = k;
            }
        }

        char ch = kAlphabet[best_k];
        if (ch != '_')
            plate.push_back(ch);
    }

    // Trả về 1 attribute chứa text (confidence để 1.0f cho đơn giản)
    NvDsInferAttribute attr;
    // std::memset(&attr, 0, sizeof(attr));
    attr.attributeIndex = 0;  // "plate_text"
    attr.attributeValue = 0;
    attr.attributeConfidence = 1.0f;
    attr.attributeLabel = strdup(plate.c_str());  // DS sẽ g_free/free giúp
    attrList.push_back(attr);

    descString.append("[license_plate] ");
    descString.append(attr.attributeLabel);
    descString.append("");

    return true;
}

// Bắt buộc để DS verify prototype lúc load .so
CHECK_CUSTOM_CLASSIFIER_PARSE_FUNC_PROTOTYPE(NvDsInferClassifierParseCustomFastPlateOCR);
