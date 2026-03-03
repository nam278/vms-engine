#pragma once
#include "engine/core/config/config_types.hpp"
#include <string>

namespace engine::core::config {

/**
 * @brief Parses a YAML file into a PipelineConfig struct.
 */
class IConfigParser {
   public:
    virtual ~IConfigParser() = default;
    virtual bool parse(const std::string& file_path, PipelineConfig& config) = 0;
};

}  // namespace engine::core::config
