#include "engine/infrastructure/control/runtime_control_handler.hpp"

#include "engine/core/utils/logger.hpp"

#include <algorithm>

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
        } else if (message_type == "get_element_property") {
            response = get_property(requested_pipeline_id, message.value("element_id", ""),
                                    message.value("property", ""));
        } else if (message_type == "set_element_properties") {
            response =
                set_properties(requested_pipeline_id, message.value("element_id", ""), message);
        } else {
            response = {400,
                        "Bad Request",
                        {{"error", "unsupported_message_type"}, {"type", message_type}}};
        }

        return decorate_response(std::move(response), request_id, message_type);
    } catch (const std::exception& ex) {
        return {400, "Bad Request", {{"error", "invalid_json"}, {"message", ex.what()}}};
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

ControlResponse RuntimeControlHandler::pipeline_not_found(
    const std::string& requested_pipeline_id) const {
    return {404,
            "Not Found",
            {{"error", "pipeline_not_found"}, {"pipeline_id", requested_pipeline_id}}};
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