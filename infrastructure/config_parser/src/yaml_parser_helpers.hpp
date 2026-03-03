#pragma once
/**
 * @file yaml_parser_helpers.hpp
 * @brief Shared utilities for YAML config parsing (src-local only).
 *
 * This header is NOT part of the public include directory.
 * It is included by yaml_parser_*.cpp files within config_parser/src/.
 */

#include <yaml-cpp/yaml.h>
#include <string>
#include <vector>

namespace engine::infrastructure::config_parser::helpers {

// ── Scalar helpers ─────────────────────────────────────────────────

inline int yaml_int(const YAML::Node& node, const std::string& key, int def) {
    if (node[key])
        return node[key].as<int>(def);
    return def;
}

inline float yaml_float(const YAML::Node& node, const std::string& key, float def) {
    if (node[key])
        return node[key].as<float>(def);
    return def;
}

inline double yaml_double(const YAML::Node& node, const std::string& key, double def) {
    if (node[key])
        return node[key].as<double>(def);
    return def;
}

inline bool yaml_bool(const YAML::Node& node, const std::string& key, bool def) {
    if (node[key])
        return node[key].as<bool>(def);
    return def;
}

inline std::string yaml_str(const YAML::Node& node, const std::string& key,
                            const std::string& def = "") {
    if (node[key])
        return node[key].as<std::string>(def);
    return def;
}

// ── Vector helpers ─────────────────────────────────────────────────

inline std::vector<std::string> yaml_string_list(const YAML::Node& node, const std::string& key) {
    std::vector<std::string> result;
    if (node[key] && node[key].IsSequence()) {
        for (const auto& item : node[key]) {
            result.push_back(item.as<std::string>(""));
        }
    }
    return result;
}

}  // namespace engine::infrastructure::config_parser::helpers
