#pragma once
#include <string>

namespace engine::core::runtime {

/**
 * @brief Allows runtime modification of GStreamer element properties.
 */
class IRuntimeParamManager {
   public:
    virtual ~IRuntimeParamManager() = default;
    virtual bool set_param(const std::string& element_id, const std::string& property,
                           const std::string& value) = 0;
    virtual std::string get_param(const std::string& element_id, const std::string& property) = 0;
};

}  // namespace engine::core::runtime
