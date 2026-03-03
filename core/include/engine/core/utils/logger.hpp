#pragma once
#include <spdlog/spdlog.h>

// Global macros — NEVER use std::cout or printf in library code
#define LOG_T(...) SPDLOG_TRACE(__VA_ARGS__)
#define LOG_D(...) SPDLOG_DEBUG(__VA_ARGS__)
#define LOG_I(...) SPDLOG_INFO(__VA_ARGS__)
#define LOG_W(...) SPDLOG_WARN(__VA_ARGS__)
#define LOG_E(...) SPDLOG_ERROR(__VA_ARGS__)
#define LOG_C(...) SPDLOG_CRITICAL(__VA_ARGS__)
