#include <cstring>
#include <iostream>
#include <vector>
#include <string>
#include <cuda_fp16.h>
#include "nvdsinfer_custom_impl.h"

// ==== Danh sách 24 nhãn theo đúng thứ tự output sigmoid ====
static const std::vector<std::string> labels_24 = {
    "hairLong",         "hairShort",       "upperBodyBlack", "upperBodyBlue",  "upperBodyBrown",
    "upperBodyGreen",   "upperBodyGrey",   "upperBodyRed",   "upperBodyWhite", "lowerBodyBlack",
    "lowerBodyBlue",    "lowerBodyBrown",  "lowerBodyGreen", "lowerBodyGrey",  "lowerBodyRed",
    "lowerBodyWhite",   "lowerBodyYellow", "personalLess30", "personalLess45", "personalLess60",
    "personalLarger60", "personalLess15",  "personalMale",   "personalFemale"};

static const std::vector<std::string> group_names_24 = {"hair", "upper_color", "lower_color", "age",
                                                        "gender"};

// ==== Ánh xạ: chỉ số nhãn → chỉ số nhóm ====
static const std::vector<int> label_to_group_24 = {
    0, 0,                    // hair: long, short
    1, 1, 1, 1, 1, 1, 1,     // upper_color: black, blue, brown, green, grey, red, white
    2, 2, 2, 2, 2, 2, 2, 2,  // lower_color: black, blue, brown, green, grey, red, white, yellow
    3, 3, 3, 3, 3,           // age: less30, less45, less60, larger60, less15
    4, 4                     // gender: male, female
};

extern "C" bool NvDsInferClassifierParseCustomPersonAttr24Label(
    const std::vector<NvDsInferLayerInfo>& outputLayersInfo,
    const NvDsInferNetworkInfo& networkInfo, float classifierThreshold,
    std::vector<NvDsInferAttribute>& attrList, std::string& descString) {
    if (outputLayersInfo.size() != 1) {
        std::cerr << "[ERROR] Expected 1 output layer for 24-label person attributes\n";
        return false;
    }

    const auto& layer = outputLayersInfo[0];
    NvDsInferDimsCHW dims;
    getDimsCHWFromDims(dims, layer.inferDims);

    if (dims.c != labels_24.size()) {
        std::cerr << "[ERROR] Output size mismatch: expected " << labels_24.size() << ", got "
                  << dims.c << std::endl;
        return false;
    }

    std::vector<float> probVec(labels_24.size());

    // FLOAT (FP32)
    if (layer.dataType == NvDsInferDataType::FLOAT) {
        const float* buf = static_cast<float*>(layer.buffer);
        for (size_t i = 0; i < labels_24.size(); ++i)
            probVec[i] = buf[i];
    }
    // HALF (FP16)
    else if (layer.dataType == NvDsInferDataType::HALF) {
        const __half* buf = static_cast<__half*>(layer.buffer);
        for (size_t i = 0; i < labels_24.size(); ++i)
            probVec[i] = __half2float(buf[i]);
    } else {
        std::cerr << "[ERROR] Unsupported data type in output layer for 24-label parser.\n";
        return false;
    }

    const int num_groups = group_names_24.size();
    std::vector<float> maxProb(num_groups, 0.0f);
    std::vector<int> bestLabel(num_groups, -1);

    // Tìm nhãn có confidence cao nhất trong mỗi nhóm
    for (size_t i = 0; i < labels_24.size(); ++i) {
        int group = label_to_group_24[i];
        float prob = probVec[i];

        if (prob > maxProb[group]) {
            maxProb[group] = prob;
            bestLabel[group] = static_cast<int>(i);
        }
    }

    // Thêm attributes vào kết quả
    for (int group = 0; group < num_groups; ++group) {
        int idx = bestLabel[group];
        if (idx >= 0 && maxProb[group] >= classifierThreshold) {
            NvDsInferAttribute attr;
            attr.attributeIndex = group;
            attr.attributeValue = idx;
            attr.attributeConfidence = maxProb[group];
            attr.attributeLabel = strdup(labels_24[idx].c_str());

            attrList.push_back(attr);

            // Build description string
            descString.append("[");
            descString.append(group_names_24[group]);
            descString.append("] ");
            descString.append(attr.attributeLabel);
            descString.append(" ");
        }
    }

    return true;
}

CHECK_CUSTOM_CLASSIFIER_PARSE_FUNC_PROTOTYPE(NvDsInferClassifierParseCustomPersonAttr24Label);
