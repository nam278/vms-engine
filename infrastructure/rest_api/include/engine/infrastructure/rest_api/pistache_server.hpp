#pragma once
/**
 * @file pistache_server.hpp
 * @brief REST API server stub for runtime pipeline control.
 *
 * Placeholder for Pistache HTTP server integration.
 * Will expose endpoints: GET /health, POST /pipeline/start, POST /pipeline/stop,
 * POST /params/{key}, GET /state.
 */
#include "engine/core/pipeline/ipipeline_manager.hpp"
#include <string>
#include <memory>

namespace engine::infrastructure::rest_api {

/**
 * @brief HTTP REST server wrapping IPipelineManager for runtime control.
 *
 * Stub — will be fully implemented when Pistache is added as a dependency.
 */
class PistacheServer {
   public:
    /**
     * @param manager Pipeline manager to control via REST endpoints.
     * @param address Bind address (e.g. "0.0.0.0").
     * @param port    Listen port (e.g. 8080).
     */
    PistacheServer(engine::core::pipeline::IPipelineManager* manager, const std::string& address,
                   int port);
    ~PistacheServer();

    /** @brief Start the HTTP server (non-blocking). */
    bool start();

    /** @brief Stop the HTTP server gracefully. */
    void stop();

   private:
    engine::core::pipeline::IPipelineManager* manager_;
    std::string address_;
    int port_;
    bool running_ = false;
};

}  // namespace engine::infrastructure::rest_api
