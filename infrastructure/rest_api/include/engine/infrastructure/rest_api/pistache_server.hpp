#pragma once
/**
 * @file pistache_server.hpp
 * @brief Lightweight HTTP control server for runtime pipeline/property control.
 *
 * The class name is kept for compatibility, but the implementation uses
 * GLib/GIO sockets instead of the Pistache framework.
 */
#include "engine/infrastructure/control/runtime_control_handler.hpp"

#include <gio/gio.h>

#include <atomic>
#include <memory>
#include <string>

namespace engine::infrastructure::rest_api {

/**
 * @brief HTTP server wrapping IPipelineManager + IRuntimeParamManager.
 */
class PistacheServer {
   public:
    /**
     * @param handler            Shared runtime control handler.
     * @param address            Bind address (e.g. "0.0.0.0").
     * @param port               Listen port (e.g. 18080).
     */
    PistacheServer(std::shared_ptr<engine::infrastructure::control::RuntimeControlHandler> handler,
                   const std::string& address, int port);
    ~PistacheServer();

    /** @brief Start the HTTP server (non-blocking). */
    bool start();

    /** @brief Stop the HTTP server gracefully. */
    void stop();

   private:
    std::shared_ptr<engine::infrastructure::control::RuntimeControlHandler> handler_;
    std::string address_;
    int port_;
    GSocketService* service_ = nullptr;
    std::atomic<bool> running_{false};

    static gboolean on_incoming(GSocketService* service, GSocketConnection* connection,
                                GObject* source_object, gpointer user_data);
    void handle_connection(GSocketConnection* connection);
};

}  // namespace engine::infrastructure::rest_api
