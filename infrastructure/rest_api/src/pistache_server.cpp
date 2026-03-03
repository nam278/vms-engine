/**
 * @file pistache_server.cpp
 * @brief Stub REST API server implementation.
 *
 * Full Pistache integration deferred until dependency is added to CMake.
 * All methods log warnings and return safe defaults.
 */
#include "engine/infrastructure/rest_api/pistache_server.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::infrastructure::rest_api {

PistacheServer::PistacheServer(engine::core::pipeline::IPipelineManager* manager,
                               const std::string& address, int port)
    : manager_(manager), address_(address), port_(port) {
    LOG_D("PistacheServer stub constructed ({}:{})", address_, port_);
}

PistacheServer::~PistacheServer() {
    if (running_) {
        stop();
    }
}

bool PistacheServer::start() {
    LOG_W(
        "PistacheServer::start() — stub, Pistache not yet integrated. "
        "Would listen on {}:{}",
        address_, port_);
    running_ = true;
    return true;  // Stub returns success so caller flow continues
}

void PistacheServer::stop() {
    LOG_W("PistacheServer::stop() — stub, nothing to stop");
    running_ = false;
}

}  // namespace engine::infrastructure::rest_api
