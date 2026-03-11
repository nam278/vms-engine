#pragma once

#include <cctype>
#include <string>

namespace engine::pipeline {

inline std::string normalize_camera_id_for_element_name(const std::string& camera_id) {
    std::string normalized;
    normalized.reserve(camera_id.size());

    for (unsigned char ch : camera_id) {
        if (std::isalnum(ch) != 0U) {
            normalized.push_back(static_cast<char>(ch));
        } else {
            normalized.push_back('_');
        }
    }

    if (normalized.empty()) {
        normalized = "camera";
    }

    return normalized;
}

inline std::string make_source_element_name(const std::string& camera_id) {
    return "nvurisrcbin_" + normalize_camera_id_for_element_name(camera_id);
}

inline std::string make_source_bin_name(const std::string& camera_id) {
    return "srcbin_" + normalize_camera_id_for_element_name(camera_id);
}

inline std::string make_source_branch_element_name(const std::string& camera_id,
                                                   const std::string& element_id,
                                                   const std::string& element_type) {
    const std::string suffix = !element_id.empty() ? element_id : element_type;
    return normalize_camera_id_for_element_name(camera_id) + "__" +
           normalize_camera_id_for_element_name(suffix);
}

}  // namespace engine::pipeline