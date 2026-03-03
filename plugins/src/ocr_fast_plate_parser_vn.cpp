// plugins/src/ocr_fast_plate_parser.cpp
//
// Custom parser cho OCR biển số kiểu multi-head classification (fast-plate-ocr).
// Cấu hình Latin plates hiện tại:
//   - max_plate_slots = 11
//   - alphabet = "0123456789ABCDEFGHIJKLMNPQRSTUVWXYZ_-."
//   - pad_char = '_'
// Model output kỳ vọng: [S, C] hoặc [1, S, C] hay flatten [S*C] (logits hoặc softmax).
//
// Dùng với DeepStream (nvinfer hoặc nvinferserver):
//   nvinfer:
//     [property]
//     custom-lib-path=/opt/lantana/build/bin/plugins/libocr_fast_plate_parser_vn.so
//     parse-classifier-func-name=NvDsInferClassifierParseCustomFastPlateOCR
//
//   nvinferserver:
//     postprocess { classification { custom_parse_classifier_func:
//       "NvDsInferClassifierParseCustomFastPlateOCR" } }
//     custom_lib { path: "/opt/lantana/lib/libocr_fast_plate_parser_vn.so" }

#include <cstring>
#include <iostream>
#include <vector>
#include <string>
#include <cuda_fp16.h>
#include "nvdsinfer_custom_impl.h"

// ====== Plate config (cố định theo yêu cầu mới) ======
static const char* kAlphabet = "0123456789ABCDEFGHIJKLMNPQRSTUVWXYZ_-.";
static inline int kClasses() {
    return 38;  // 10 digits + 25 letters (no 'O') + '_' + '-' + '.' = 38
}
static inline int kSlots() {
    return 11;  // max_plate_slots
}
// Pad char để bỏ khi ghép kết quả
static inline char kPadChar() {
    return '_';
}

// ====== Utils nhỏ ======
static inline bool toFloatVector(const NvDsInferLayerInfo& L, std::vector<float>& out) {
    const int N = static_cast<int>(out.size());
    if (L.dataType == NvDsInferDataType::FLOAT) {
        const float* p = static_cast<const float*>(L.buffer);
        for (int i = 0; i < N; ++i)
            out[i] = p[i];
        return true;
    } else if (L.dataType == NvDsInferDataType::HALF) {
        const __half* p = static_cast<const __half*>(L.buffer);
        for (int i = 0; i < N; ++i)
            out[i] = __half2float(p[i]);
        return true;
    } else {
        std::cerr << "[FastPlateOCR] Unsupported output dtype (expect FP32/FP16)\n";
        return false;
    }
}

// ====== Parser chính ======
extern "C" bool NvDsInferClassifierParseCustomFastPlateOCR(
    const std::vector<NvDsInferLayerInfo>& outLayers, const NvDsInferNetworkInfo& /*networkInfo*/,
    float /*classifierThreshold*/, std::vector<NvDsInferAttribute>& attrList,
    std::string& descString) {
    if (outLayers.size() != 1) {
        std::cerr << "[FastPlateOCR] Expect exactly 1 output layer (S x C), got "
                  << outLayers.size() << "\n";
        return false;
    }

    const NvDsInferLayerInfo& L = outLayers[0];

    // Suy ra (S, C) từ inferDims
    int d[8] = {0};
    for (unsigned i = 0; i < L.inferDims.numDims && i < 8; ++i)
        d[i] = L.inferDims.d[i];

    int S = 0, C = 0;  // slots, classes
    if (L.inferDims.numDims == 2) {
        // [S, C]
        S = d[0];
        C = d[1];
    } else if (L.inferDims.numDims == 3) {
        // [N, S, C] với N=1
        S = d[1];
        C = d[2];
    } else if (L.inferDims.numDims == 1) {
        // flatten: [S*C]
        C = kClasses();
        const int total = d[0];
        if (C > 0 && total % C == 0)
            S = total / C;
    } else {
        std::cerr << "[FastPlateOCR] Bad output dims (numDims=" << L.inferDims.numDims << ")\n";
        return false;
    }

    if (S <= 0 || C <= 0) {
        std::cerr << "[FastPlateOCR] Bad output shape (S=" << S << ", C=" << C << ")\n";
        return false;
    }

    if (C != kClasses()) {
        std::cerr << "[FastPlateOCR] Class mismatch: model C=" << C << " vs expected " << kClasses()
                  << " — update kAlphabet/kClasses if your model config changed.\n";
        // vẫn tiếp tục parse để giúp debug
    }

    // Chuẩn bị buffer [S, C]
    std::vector<float> logits(static_cast<size_t>(S) * static_cast<size_t>(C));
    if (!toFloatVector(L, logits))
        return false;

    // Argmax theo từng slot, ghép chuỗi bỏ padChar ('_')
    std::string plate;
    plate.reserve(S);
    for (int s = 0; s < S; ++s) {
        const float* row = &logits[static_cast<size_t>(s) * static_cast<size_t>(C)];

        int best_k = 0;
        float best_v = row[0];
        for (int k = 1; k < C; ++k) {
            const float v = row[k];
            if (v > best_v) {
                best_v = v;
                best_k = k;
            }
        }

        // Bảo vệ index khỏi vượt quá alphabet khi C != kClasses()
        const int clamped =
            (best_k >= 0 && best_k < static_cast<int>(std::strlen(kAlphabet))) ? best_k : 0;
        const char ch = kAlphabet[clamped];
        if (ch != kPadChar())
            plate.push_back(ch);
    }

    // Trả về duy nhất 1 attribute chứa chuỗi plate (confidence đặt 1.0)
    NvDsInferAttribute attr{};
    attr.attributeIndex = 0;                      // "plate_text"
    attr.attributeValue = 0;                      // không dùng
    attr.attributeConfidence = 1.0f;              // có thể thay bằng min(best_v) nếu muốn
    attr.attributeLabel = strdup(plate.c_str());  // DeepStream sẽ free (xem NvDsInferAttribute)
                                                  // :contentReference[oaicite:2]{index=2}
    attrList.push_back(attr);

    descString.append("[license_plate] ");
    descString.append(plate);

    return true;
}

// Bắt buộc để DS verify prototype lúc load .so
CHECK_CUSTOM_CLASSIFIER_PARSE_FUNC_PROTOTYPE(NvDsInferClassifierParseCustomFastPlateOCR);
