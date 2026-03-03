#include "engine/core/utils/spdlog_logger.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <vector>
#include <memory>

namespace engine::core::utils {

void initialize_logger(const std::string& log_level, const std::string& log_file) {
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    if (!log_file.empty()) {
        sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file, true));
    }

    auto logger = std::make_shared<spdlog::logger>("vms_engine", sinks.begin(), sinks.end());
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");

    if (log_level == "TRACE")
        logger->set_level(spdlog::level::trace);
    else if (log_level == "DEBUG")
        logger->set_level(spdlog::level::debug);
    else if (log_level == "WARN")
        logger->set_level(spdlog::level::warn);
    else if (log_level == "ERROR")
        logger->set_level(spdlog::level::err);
    else if (log_level == "CRITICAL")
        logger->set_level(spdlog::level::critical);
    else
        logger->set_level(spdlog::level::info);

    spdlog::set_default_logger(logger);
    spdlog::flush_every(std::chrono::seconds(3));
}

}  // namespace engine::core::utils
