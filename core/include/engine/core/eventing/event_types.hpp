#pragma once
#include <string_view>

namespace engine::core::eventing {

/** @brief End-of-stream event key emitted from GstBus EOS messages. */
inline constexpr std::string_view ON_EOS = "on_eos";

/** @brief Detection event from pad probe on inference output. */
inline constexpr std::string_view ON_DETECT = "on_detect";

/** @brief State change event from GstBus state-changed messages. */
inline constexpr std::string_view ON_STATE_CHANGE = "on_state_change";

/** @brief Error event from GstBus error messages. */
inline constexpr std::string_view ON_ERROR = "on_error";

}  // namespace engine::core::eventing
