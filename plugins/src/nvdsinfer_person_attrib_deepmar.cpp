#include <cstring>
#include <iostream>
#include <vector>
#include <string>
#include <cuda_fp16.h>
#include "nvdsinfer_custom_impl.h"

// ==== Danh sách nhãn theo đúng thứ tự output sigmoid ====
static const std::vector<std::string> labels = {
    "accessorySunglasses",  "accessoryHat",         "hairLong",          "hairShort",
    "upperBodyShortSleeve", "upperBodyTshirt",      "upperBodyBlack",    "upperBodyBlue",
    "upperBodyBrown",       "upperBodyGreen",       "upperBodyGrey",     "upperBodyRed",
    "upperBodyWhite",       "upperBodyLongSleeve",  "upperBodyNoSleeve", "lowerBodyJeans",
    "lowerBodyShorts",      "lowerBodyShortSkirt",  "lowerBodyTrousers", "lowerBodyBlack",
    "lowerBodyBlue",        "lowerBodyBrown",       "lowerBodyGreen",    "lowerBodyGrey",
    "lowerBodyRed",         "lowerBodyWhite",       "lowerBodyYellow",   "lowerBodyLongSkirt",
    "footwearSandals",      "footwearShoes",        "footwearSneaker",   "footwearBoots",
    "carryingBackpack",     "carryingMessengerBag", "carryingNothing",   "carryingPlasticBags",
    "carryingLuggageCase",  "personalLess30",       "personalLess45",    "personalLess60",
    "personalLarger60",     "personalLess15",       "personalMale",      "personalFemale"};

static const std::vector<std::string> group_names = {
    "accessory",   "hair",     "upper_type", "upper_color", "lower_type",
    "lower_color", "footwear", "carrying",   "age",         "gender"};

// ==== Ánh xạ: chỉ số nhãn → chỉ số nhóm ====
static const std::vector<int> label_to_group = {
    0, 0,                    // accessory: sunglasses, hat
    1, 1,                    // hair: long, short
    2, 2, 2, 2,              // upper_type: short_sleeve, tshirt, long_sleeve, no_sleeve
    3, 3, 3, 3, 3, 3, 3,     // upper_color: black, blue, brown, green, grey, red, white
    4, 4, 4, 4, 4,           // lower_type: jeans, shorts, short_skirt, trousers, long_skirt
    5, 5, 5, 5, 5, 5, 5, 5,  // lower_color: black, blue, brown, green, grey, red, white, yellow
    6, 6, 6, 6,              // footwear: sandals, shoes, sneaker, boots
    7, 7, 7, 7, 7,  // carrying: backpack, messenger_bag, nothing, plastic_bags, luggage_case
    8, 8, 8, 8, 8,  // age: less30, less45, less60, larger60, less15
    9, 9            // gender: male, female
};

extern "C" bool NvDsInferClassifierParseCustomPersonAttrDeepMar(
    const std::vector<NvDsInferLayerInfo>& outputLayersInfo,
    const NvDsInferNetworkInfo& networkInfo, float classifierThreshold,
    std::vector<NvDsInferAttribute>& attrList, std::string& descString) {
    if (outputLayersInfo.size() != 1) {
        std::cerr << "[ERROR] Expected 1 output layer\n";
        return false;
    }

    const auto& layer = outputLayersInfo[0];
    NvDsInferDimsCHW dims;
    getDimsCHWFromDims(dims, layer.inferDims);
    if (dims.c != labels.size()) {
        std::cerr << "[ERROR] Output size mismatch: expected " << labels.size() << ", got "
                  << dims.c << std::endl;
        return false;
    }

    std::vector<float> probVec(labels.size());

    // FLOAT (FP32)
    if (layer.dataType == NvDsInferDataType::FLOAT) {
        const float* buf = static_cast<float*>(layer.buffer);
        for (size_t i = 0; i < labels.size(); ++i)
            probVec[i] = buf[i];
    }
    // HALF (FP16)
    else if (layer.dataType == NvDsInferDataType::HALF) {
        const __half* buf = static_cast<__half*>(layer.buffer);
        for (size_t i = 0; i < labels.size(); ++i)
            probVec[i] = __half2float(buf[i]);
    } else {
        std::cerr << "[ERROR] Unsupported data type in output layer.\n";
        return false;
    }

    const int num_groups = group_names.size();
    std::vector<float> maxProb(num_groups, 0.0f);
    std::vector<int> bestLabel(num_groups, -1);

    for (size_t i = 0; i < labels.size(); ++i) {
        int group = label_to_group[i];
        float prob = probVec[i];

        if (prob > maxProb[group]) {
            maxProb[group] = prob;
            bestLabel[group] = static_cast<int>(i);
        }
    }

    for (int group = 0; group < num_groups; ++group) {
        int idx = bestLabel[group];
        if (idx >= 0 && maxProb[group] >= classifierThreshold) {
            NvDsInferAttribute attr;
            attr.attributeIndex = group;
            attr.attributeValue = idx;
            attr.attributeConfidence = maxProb[group];
            attr.attributeLabel = strdup(labels[idx].c_str());

            attrList.push_back(attr);

            descString.append("[");
            descString.append(group_names[group]);
            descString.append("] ");
            descString.append(attr.attributeLabel);
            descString.append(" ");
        }
    }

    return true;
}

CHECK_CUSTOM_CLASSIFIER_PARSE_FUNC_PROTOTYPE(NvDsInferClassifierParseCustomPersonAttrDeepMar);
