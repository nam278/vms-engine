/**
 * @file pistache_server.cpp
 * @brief Lightweight GIO-based HTTP control server implementation.
 */
#include "engine/infrastructure/rest_api/pistache_server.hpp"
#include "engine/core/utils/logger.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace engine::infrastructure::rest_api {

namespace {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
};

struct HttpResponse {
    int status_code = 200;
    std::string status_text = "OK";
    std::string body = "{}";
};

std::string trim_copy(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string lowercase_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string strip_query_string(const std::string& path) {
    const auto query_pos = path.find('?');
    return query_pos == std::string::npos ? path : path.substr(0, query_pos);
}

std::vector<std::string> split_path(const std::string& path) {
    std::vector<std::string> segments;
    std::stringstream ss(path);
    std::string segment;
    while (std::getline(ss, segment, '/')) {
        if (!segment.empty()) {
            segments.push_back(segment);
        }
    }
    return segments;
}

HttpResponse make_json_response(int status_code, std::string status_text,
                                const nlohmann::json& body) {
    HttpResponse response;
    response.status_code = status_code;
    response.status_text = std::move(status_text);
    nlohmann::json normalized_body = body;
    if (!normalized_body.contains("status_code")) {
        normalized_body["status_code"] = status_code;
    }
    if (!normalized_body.contains("ok")) {
        normalized_body["ok"] = status_code >= 200 && status_code < 300;
    }
    response.body = normalized_body.dump();
    return response;
}

HttpResponse make_control_json_response(
    const engine::infrastructure::control::ControlResponse& control_response) {
    nlohmann::json body = control_response.body;
    body["status_code"] = control_response.status_code;
    body["ok"] = control_response.status_code >= 200 && control_response.status_code < 300;
    return make_json_response(control_response.status_code, control_response.status_text, body);
}

bool read_http_request(GSocketConnection* connection, HttpRequest& request) {
    GInputStream* input = g_io_stream_get_input_stream(G_IO_STREAM(connection));
    std::string raw;
    char buffer[4096];

    while (raw.find("\r\n\r\n") == std::string::npos && raw.size() < 65536U) {
        GError* error = nullptr;
        const gssize bytes_read =
            g_input_stream_read(input, buffer, sizeof(buffer), nullptr, &error);
        if (bytes_read <= 0) {
            if (error != nullptr) {
                LOG_W("Control API: failed to read request header: {}", error->message);
                g_error_free(error);
            }
            return false;
        }
        raw.append(buffer, static_cast<std::size_t>(bytes_read));
    }

    const auto header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return false;
    }

    const std::string header_block = raw.substr(0, header_end);
    std::string body = raw.substr(header_end + 4);
    std::istringstream header_stream(header_block);
    std::string request_line;
    if (!std::getline(header_stream, request_line)) {
        return false;
    }
    if (!request_line.empty() && request_line.back() == '\r') {
        request_line.pop_back();
    }

    std::istringstream request_line_stream(request_line);
    std::string http_version;
    request_line_stream >> request.method >> request.path >> http_version;
    if (request.method.empty() || request.path.empty()) {
        return false;
    }

    std::size_t content_length = 0;
    std::string header_line;
    while (std::getline(header_stream, header_line)) {
        if (!header_line.empty() && header_line.back() == '\r') {
            header_line.pop_back();
        }
        const auto colon_pos = header_line.find(':');
        if (colon_pos == std::string::npos) {
            continue;
        }
        const std::string name = lowercase_copy(trim_copy(header_line.substr(0, colon_pos)));
        const std::string value = trim_copy(header_line.substr(colon_pos + 1));
        if (name == "content-length") {
            content_length = static_cast<std::size_t>(std::stoul(value));
        }
    }

    while (body.size() < content_length) {
        GError* error = nullptr;
        const gssize bytes_read =
            g_input_stream_read(input, buffer, sizeof(buffer), nullptr, &error);
        if (bytes_read <= 0) {
            if (error != nullptr) {
                LOG_W("Control API: failed to read request body: {}", error->message);
                g_error_free(error);
            }
            return false;
        }
        body.append(buffer, static_cast<std::size_t>(bytes_read));
    }

    request.body = body.substr(0, content_length);
    return true;
}

bool write_http_response(GSocketConnection* connection, const HttpResponse& response) {
    GOutputStream* output = g_io_stream_get_output_stream(G_IO_STREAM(connection));

    std::ostringstream builder;
    builder << "HTTP/1.1 " << response.status_code << ' ' << response.status_text << "\r\n"
            << "Content-Type: application/json\r\n"
            << "Content-Length: " << response.body.size() << "\r\n"
            << "Connection: close\r\n\r\n"
            << response.body;

    const std::string wire = builder.str();
    gsize bytes_written = 0;
    GError* error = nullptr;
    const gboolean ok = g_output_stream_write_all(output, wire.data(), wire.size(), &bytes_written,
                                                  nullptr, &error);
    if (!ok) {
        if (error != nullptr) {
            LOG_W("Control API: failed to write response: {}", error->message);
            g_error_free(error);
        }
        return false;
    }

    g_output_stream_flush(output, nullptr, nullptr);
    return bytes_written == wire.size();
}

void close_http_connection(GSocketConnection* connection) {
    GError* error = nullptr;
    if (!g_io_stream_close(G_IO_STREAM(connection), nullptr, &error) && error != nullptr) {
        LOG_D("Control API: failed to close connection cleanly: {}", error->message);
        g_error_free(error);
    }
}

}  // namespace

PistacheServer::PistacheServer(
    std::shared_ptr<engine::infrastructure::control::RuntimeControlHandler> handler,
    const std::string& address, int port)
    : handler_(std::move(handler)), address_(address), port_(port) {
    LOG_D("Control API server constructed ({}:{})", address_, port_);
}

PistacheServer::~PistacheServer() {
    if (running_.load()) {
        stop();
    }
}

bool PistacheServer::start() {
    if (running_.load()) {
        return true;
    }
    if (handler_ == nullptr) {
        LOG_E("Control API: missing runtime control handler");
        return false;
    }

    service_ = g_socket_service_new();
    if (service_ == nullptr) {
        LOG_E("Control API: failed to create GSocketService");
        return false;
    }

    GInetAddress* inet_address = g_inet_address_new_from_string(address_.c_str());
    if (inet_address == nullptr) {
        inet_address = g_inet_address_new_any(G_SOCKET_FAMILY_IPV4);
    }

    GSocketAddress* socket_address =
        g_inet_socket_address_new(inet_address, static_cast<guint16>(port_));
    g_object_unref(inet_address);

    GError* error = nullptr;
    const gboolean ok = g_socket_listener_add_address(G_SOCKET_LISTENER(service_), socket_address,
                                                      G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP,
                                                      nullptr, nullptr, &error);
    g_object_unref(socket_address);
    if (!ok) {
        LOG_E("Control API: failed to bind {}:{} - {}", address_, port_,
              error ? error->message : "unknown error");
        if (error != nullptr) {
            g_error_free(error);
        }
        g_clear_object(&service_);
        return false;
    }

    g_signal_connect(service_, "incoming", G_CALLBACK(&PistacheServer::on_incoming), this);
    g_socket_service_start(service_);
    running_.store(true);
    LOG_I("Control API: listening on http://{}:{} for pipeline '{}'", address_, port_,
          handler_->pipeline_id());
    return true;
}

void PistacheServer::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (service_ != nullptr) {
        g_socket_service_stop(service_);
        g_clear_object(&service_);
    }
    LOG_I("Control API: stopped");
}

gboolean PistacheServer::on_incoming(GSocketService* /*service*/, GSocketConnection* connection,
                                     GObject* /*source_object*/, gpointer user_data) {
    auto* self = static_cast<PistacheServer*>(user_data);
    if (self == nullptr || !self->running_.load()) {
        return FALSE;
    }
    self->handle_connection(connection);
    return TRUE;
}

void PistacheServer::handle_connection(GSocketConnection* connection) {
    HttpRequest request;
    HttpResponse response =
        make_json_response(400, "Bad Request", nlohmann::json{{"error", "invalid_request"}});
    const auto send_response = [&](const HttpResponse& http_response) {
        write_http_response(connection, http_response);
        close_http_connection(connection);
    };

    if (!read_http_request(connection, request)) {
        send_response(response);
        return;
    }

    const std::string path = strip_query_string(request.path);
    const std::vector<std::string> segments = split_path(path);

    if (request.method == "GET" && path == "/health") {
        const auto control_response = handler_->get_health();
        response = make_control_json_response(control_response);
        send_response(response);
        return;
    }

    if (segments.size() < 5 || segments[0] != "api" || segments[1] != "v1" ||
        segments[2] != "pipelines") {
        response =
            make_json_response(404, "Not Found", nlohmann::json{{"error", "route_not_found"}});
        send_response(response);
        return;
    }

    const std::string& requested_pipeline_id = segments[3];
    if (requested_pipeline_id != handler_->pipeline_id()) {
        response = make_json_response(
            404, "Not Found",
            nlohmann::json{{"error", "pipeline_not_found"},
                           {"error_code", "SRCCTL_PIPELINE_NOT_FOUND"},
                           {"message", "requested pipeline id does not match active pipeline"},
                           {"pipeline_id", requested_pipeline_id}});
        send_response(response);
        return;
    }

    if (request.method == "GET" && segments.size() == 5 && segments[4] == "state") {
        const auto control_response = handler_->get_pipeline_state(requested_pipeline_id);
        response = make_control_json_response(control_response);
        send_response(response);
        return;
    }

    if (segments.size() == 5 && segments[4] == "sources") {
        if (request.method == "GET") {
            const auto control_response = handler_->list_sources(requested_pipeline_id);
            response = make_control_json_response(control_response);
            send_response(response);
            return;
        }

        if (request.method == "POST") {
            try {
                const auto body = request.body.empty() ? nlohmann::json::object()
                                                       : nlohmann::json::parse(request.body);
                const auto control_response = handler_->add_source(requested_pipeline_id, body);
                response = make_control_json_response(control_response);
                send_response(response);
                return;
            } catch (const std::exception& ex) {
                response =
                    make_json_response(400, "Bad Request",
                                       nlohmann::json{{"error", "invalid_json"},
                                                      {"error_code", "SRCCTL_INVALID_REQUEST"},
                                                      {"message", ex.what()}});
                send_response(response);
                return;
            }
        }
    }

    if (request.method == "DELETE" && segments.size() == 6 && segments[4] == "sources") {
        const auto control_response = handler_->remove_source(requested_pipeline_id, segments[5]);
        response = make_control_json_response(control_response);
        send_response(response);
        return;
    }

    if (segments.size() >= 7 && segments[4] == "elements" && segments[6] == "properties") {
        const std::string& element_id = segments[5];

        if (request.method == "GET" && segments.size() == 8) {
            const std::string property = segments[7];
            const auto control_response =
                handler_->get_property(requested_pipeline_id, element_id, property);
            response = make_control_json_response(control_response);
            send_response(response);
            return;
        }

        if ((request.method == "PATCH" || request.method == "PUT" || request.method == "POST") &&
            segments.size() == 7) {
            try {
                const auto body = request.body.empty() ? nlohmann::json::object()
                                                       : nlohmann::json::parse(request.body);
                const auto control_response =
                    handler_->set_properties(requested_pipeline_id, element_id, body);
                response = make_control_json_response(control_response);
                send_response(response);
                return;
            } catch (const std::exception& ex) {
                response =
                    make_json_response(400, "Bad Request",
                                       nlohmann::json{{"error", "invalid_json"},
                                                      {"error_code", "SRCCTL_INVALID_REQUEST"},
                                                      {"message", ex.what()}});
                send_response(response);
                return;
            }
        }
    }

    response = make_json_response(404, "Not Found", nlohmann::json{{"error", "route_not_found"}});
    send_response(response);
}

}  // namespace engine::infrastructure::rest_api
