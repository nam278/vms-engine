#include "engine/infrastructure/control/runtime_control_handler.hpp"

#include "engine/core/utils/logger.hpp"

#include <algorithm>

using engine::core::pipeline::RuntimeSourceErrorCode;
using engine::core::pipeline::RuntimeSourceInfo;
using engine::core::pipeline::RuntimeSourceMutationResult;

namespace engine::infrastructure::control {

namespace {

std::string state_to_string(engine::core::pipeline::PipelineState state) {
    using engine::core::pipeline::PipelineState;
    switch (state) {
        case PipelineState::Uninitialized:
            return "Uninitialized";
        case PipelineState::Ready:
            return "Ready";
        case PipelineState::Playing:
            return "Playing";
        case PipelineState::Paused:
            return "Paused";
        case PipelineState::Stopped:
            return "Stopped";
        case PipelineState::Error:
            return "Error";
        default:
            return "Unknown";
    }
}

std::string json_value_to_string(const nlohmann::json& value) {
    if (value.is_boolean()) {
        return value.get<bool>() ? "true" : "false";
    }
    if (value.is_number_integer()) {
        return std::to_string(value.get<long long>());
    }
    if (value.is_number_unsigned()) {
        return std::to_string(value.get<unsigned long long>());
    }
    if (value.is_number_float()) {
        return std::to_string(value.get<double>());
    }
    if (value.is_string()) {
        return value.get<std::string>();
    }
    return value.dump();
}

std::string http_status_text(int status_code) {
    switch (status_code) {
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 400:
            return "Bad Request";
        case 404:
            return "Not Found";
        case 409:
            return "Conflict";
        case 422:
            return "Unprocessable Entity";
        case 500:
            return "Internal Server Error";
        case 507:
            return "Insufficient Storage";
        default:
            return "OK";
    }
}

nlohmann::json source_info_to_json(const RuntimeSourceInfo& source) {
    return {{"camera_id", source.camera_id},
            {"uri", source.uri},
            {"source_index", source.source_index},
            {"is_seeded", source.is_seeded},
            {"state", source.state}};
}

}  // namespace

RuntimeControlHandler::RuntimeControlHandler(
    engine::core::pipeline::IPipelineManager* manager,
    engine::core::runtime::IRuntimeParamManager* runtime_param_mgr, std::string pipeline_id,
    std::unordered_set<std::string> allowed_params)
    : manager_(manager),
      runtime_param_mgr_(runtime_param_mgr),
      pipeline_id_(std::move(pipeline_id)),
      allowed_params_(std::move(allowed_params)) {}

const std::string& RuntimeControlHandler::pipeline_id() const {
    return pipeline_id_;
}

ControlResponse RuntimeControlHandler::get_health() const {
    return {200,
            "OK",
            {{"status", "ok"},
             {"pipeline_id", pipeline_id_},
             {"state", state_to_string(manager_->get_state())}}};
}

ControlResponse RuntimeControlHandler::get_pipeline_state(
    const std::string& requested_pipeline_id) const {
    if (requested_pipeline_id != pipeline_id_) {
        return pipeline_not_found(requested_pipeline_id);
    }

    return {200,
            "OK",
            {{"pipeline_id", pipeline_id_}, {"state", state_to_string(manager_->get_state())}}};
}

ControlResponse RuntimeControlHandler::list_sources(
    const std::string& requested_pipeline_id) const {
    if (requested_pipeline_id != pipeline_id_) {
        return pipeline_not_found(requested_pipeline_id);
    }

    return source_result_to_response(manager_->list_sources_detailed());
}

ControlResponse RuntimeControlHandler::add_source(const std::string& requested_pipeline_id,
                                                  const nlohmann::json& body) const {
    if (requested_pipeline_id != pipeline_id_) {
        return pipeline_not_found(requested_pipeline_id);
    }

    if (!body.contains("camera") || !body["camera"].is_object()) {
        return invalid_source_request("missing camera object");
    }

    const auto& camera_body = body["camera"];
    if (!camera_body.contains("id") || !camera_body["id"].is_string()) {
        return invalid_source_request("camera.id is required");
    }
    if (!camera_body.contains("uri") || !camera_body["uri"].is_string()) {
        return invalid_source_request("camera.uri is required");
    }

    engine::core::config::CameraConfig camera;
    camera.id = camera_body["id"].get<std::string>();
    camera.uri = camera_body["uri"].get<std::string>();
    return source_result_to_response(manager_->add_source_detailed(camera));
}

ControlResponse RuntimeControlHandler::remove_source(const std::string& requested_pipeline_id,
                                                     const std::string& camera_id) const {
    if (requested_pipeline_id != pipeline_id_) {
        return pipeline_not_found(requested_pipeline_id);
    }

    if (camera_id.empty()) {
        return invalid_source_request("camera_id is required");
    }

    return source_result_to_response(manager_->remove_source_detailed(camera_id));
}

ControlResponse RuntimeControlHandler::get_property(const std::string& requested_pipeline_id,
                                                    const std::string& element_id,
                                                    const std::string& property) const {
    if (requested_pipeline_id != pipeline_id_) {
        return pipeline_not_found(requested_pipeline_id);
    }

    if (!is_allowed_param(element_id, property)) {
        return {403,
                "Forbidden",
                {{"error", "property_not_allowed"},
                 {"element_id", element_id},
                 {"property", property}}};
    }

    const std::string value = runtime_param_mgr_->get_param(element_id, property);
    if (value.empty()) {
        return {
            404,
            "Not Found",
            {{"error", "property_not_found"}, {"element_id", element_id}, {"property", property}}};
    }

    return {200,
            "OK",
            {{"pipeline_id", pipeline_id_},
             {"element_id", element_id},
             {"property", property},
             {"value", value}}};
}

ControlResponse RuntimeControlHandler::set_properties(const std::string& requested_pipeline_id,
                                                      const std::string& element_id,
                                                      const nlohmann::json& body) const {
    if (requested_pipeline_id != pipeline_id_) {
        return pipeline_not_found(requested_pipeline_id);
    }

    if (!body.contains("properties") || !body["properties"].is_object()) {
        return {400, "Bad Request", {{"error", "missing_properties_object"}}};
    }

    nlohmann::json applied = nlohmann::json::object();
    for (const auto& item : body["properties"].items()) {
        if (!is_allowed_param(element_id, item.key())) {
            return {403,
                    "Forbidden",
                    {{"error", "property_not_allowed"},
                     {"element_id", element_id},
                     {"property", item.key()}}};
        }

        const std::string value = json_value_to_string(item.value());
        if (!runtime_param_mgr_->set_param(element_id, item.key(), value)) {
            return {400,
                    "Bad Request",
                    {{"error", "set_param_failed"},
                     {"element_id", element_id},
                     {"property", item.key()},
                     {"value", value}}};
        }
        applied[item.key()] = runtime_param_mgr_->get_param(element_id, item.key());
    }

    return {200,
            "OK",
            {{"pipeline_id", pipeline_id_}, {"element_id", element_id}, {"properties", applied}}};
}

ControlResponse RuntimeControlHandler::handle_message(const std::string& payload) const {
    try {
        const auto message =
            payload.empty() ? nlohmann::json::object() : nlohmann::json::parse(payload);
        const std::string request_id = message.value("request_id", "");
        const std::string message_type = message.value("type", "");
        const std::string requested_pipeline_id = message.value("pipeline_id", pipeline_id_);

        ControlResponse response;
        if (message_type == "health") {
            response = get_health();
        } else if (message_type == "get_pipeline_state") {
            response = get_pipeline_state(requested_pipeline_id);
        } else if (message_type == "list_sources") {
            response = list_sources(requested_pipeline_id);
        } else if (message_type == "add_source") {
            response = add_source(requested_pipeline_id, message);
        } else if (message_type == "remove_source") {
            response = remove_source(requested_pipeline_id, message.value("camera_id", ""));
        } else if (message_type == "get_element_property") {
            response = get_property(requested_pipeline_id, message.value("element_id", ""),
                                    message.value("property", ""));
        } else if (message_type == "set_element_properties") {
            response =
                set_properties(requested_pipeline_id, message.value("element_id", ""), message);
        } else {
            response = {400,
                        "Bad Request",
                        {{"error", "unsupported_message_type"},
                         {"error_code", engine::core::pipeline::to_string(
                                            RuntimeSourceErrorCode::InvalidRequest)},
                         {"message", "unsupported runtime control message type"},
                         {"type", message_type}}};
        }

        return decorate_response(std::move(response), request_id, message_type);
    } catch (const std::exception& ex) {
        return {400,
                "Bad Request",
                {{"error", "invalid_json"},
                 {"error_code",
                  engine::core::pipeline::to_string(RuntimeSourceErrorCode::InvalidRequest)},
                 {"message", ex.what()}}};
    }
}

bool RuntimeControlHandler::is_allowed_param(const std::string& element_id,
                                             const std::string& property) const {
    if (allowed_params_.empty()) {
        return true;
    }

    std::string normalized_property = property;
    std::replace(normalized_property.begin(), normalized_property.end(), '-', '_');
    const std::string key = element_id + "." + normalized_property;
    return allowed_params_.find(key) != allowed_params_.end();
}

ControlResponse RuntimeControlHandler::source_result_to_response(
    const RuntimeSourceMutationResult& result) const {
    nlohmann::json body = {{"pipeline_id", pipeline_id_},
                           {"message", result.message},
                           {"active_source_count", result.active_source_count}};

    if (!result.camera_id.empty()) {
        body["camera_id"] = result.camera_id;
    }

    if (result.source_index >= 0) {
        body["source_index"] = result.source_index;
    }
    if (result.source.has_value()) {
        body["source"] = source_info_to_json(*result.source);
    }
    if (!result.sources.empty()) {
        body["sources"] = nlohmann::json::array();
        for (const auto& source : result.sources) {
            body["sources"].push_back(source_info_to_json(source));
        }
    } else if (result.success && !result.source.has_value() && result.camera_id.empty()) {
        body["sources"] = nlohmann::json::array();
    }
    if (!result.dot_file_path.empty()) {
        body["dot_file"] = result.dot_file_path;
    }
    if (!result.dot_dump_warning.empty()) {
        body["warning"] = result.dot_dump_warning;
        body["warning_code"] =
            engine::core::pipeline::to_string(RuntimeSourceErrorCode::DotDumpFailed);
    }

    if (!result.success) {
        body["error"] = engine::core::pipeline::to_error_name(result.error_code);
        body["error_code"] = engine::core::pipeline::to_string(result.error_code);
    }

    return {result.http_status, http_status_text(result.http_status), std::move(body)};
}

ControlResponse RuntimeControlHandler::invalid_source_request(const std::string& message) const {
    return {
        400,
        "Bad Request",
        {{"error", engine::core::pipeline::to_error_name(RuntimeSourceErrorCode::InvalidRequest)},
         {"error_code", engine::core::pipeline::to_string(RuntimeSourceErrorCode::InvalidRequest)},
         {"message", message},
         {"pipeline_id", pipeline_id_}}};
}

ControlResponse RuntimeControlHandler::pipeline_not_found(
    const std::string& requested_pipeline_id) const {
    return {404,
            "Not Found",
            {{"error", "pipeline_not_found"},
             {"error_code",
              engine::core::pipeline::to_string(RuntimeSourceErrorCode::PipelineNotFound)},
             {"message", "requested pipeline id does not match active pipeline"},
             {"pipeline_id", requested_pipeline_id}}};
}

ControlResponse RuntimeControlHandler::decorate_response(ControlResponse response,
                                                         const std::string& request_id,
                                                         const std::string& message_type) const {
    response.body["ok"] = response.status_code >= 200 && response.status_code < 300;
    response.body["status_code"] = response.status_code;
    if (!request_id.empty()) {
        response.body["request_id"] = request_id;
    }
    if (!message_type.empty()) {
        response.body["type"] = message_type;
    }
    return response;
}

}  // namespace engine::infrastructure::control