#pragma once
#include "engine/core/config/config_types.hpp"
#include <string>
#include <vector>

namespace engine::core::config {

struct ValidationResult {
    bool success = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

/**
 * @brief Validates a PipelineConfig after parsing.
 */
class IConfigValidator {
   public:
    virtual ~IConfigValidator() = default;
    virtual ValidationResult validate(const PipelineConfig& config) = 0;
};

}  // namespace engine::core::config
