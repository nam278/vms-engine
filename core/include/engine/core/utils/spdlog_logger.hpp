#pragma once
#include <string>

namespace engine::core::utils {

/**
 * @brief Initializes spdlog with console + optional file sink.
 * @param log_level  "TRACE"|"DEBUG"|"INFO"|"WARN"|"ERROR"|"CRITICAL"
 * @param log_file   Path to log file; empty = console only.
 */
void initialize_logger(const std::string& log_level = "INFO", const std::string& log_file = "");

}  // namespace engine::core::utils
