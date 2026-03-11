#pragma once

#include "engine/core/pipeline/ipipeline_manager.hpp"
#include "engine/core/runtime/iruntime_param_manager.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_set>

namespace engine::infrastructure::control {

struct ControlResponse {
    int status_code = 200;
    std::string status_text = "OK";
    nlohmann::json body = nlohmann::json::object();
};

class RuntimeControlHandler {
   public:
    RuntimeControlHandler(engine::core::pipeline::IPipelineManager* manager,
                          engine::core::runtime::IRuntimeParamManager* runtime_param_mgr,
                          std::string pipeline_id,
                          std::unordered_set<std::string> allowed_params = {});

    const std::string& pipeline_id() const;

    ControlResponse get_health() const;
    ControlResponse get_pipeline_state(const std::string& requested_pipeline_id) const;
    ControlResponse list_sources(const std::string& requested_pipeline_id) const;
    ControlResponse add_source(const std::string& requested_pipeline_id,
                               const nlohmann::json& body) const;
    ControlResponse remove_source(const std::string& requested_pipeline_id,
                                  const std::string& camera_id) const;
    ControlResponse get_property(const std::string& requested_pipeline_id,
                                 const std::string& element_id, const std::string& property) const;
    ControlResponse set_properties(const std::string& requested_pipeline_id,
                                   const std::string& element_id, const nlohmann::json& body) const;
    ControlResponse handle_message(const std::string& payload) const;

   private:
    engine::core::pipeline::IPipelineManager* manager_;
    engine::core::runtime::IRuntimeParamManager* runtime_param_mgr_;
    std::string pipeline_id_;
    std::unordered_set<std::string> allowed_params_;

    bool is_allowed_param(const std::string& element_id, const std::string& property) const;
    ControlResponse source_result_to_response(
        const engine::core::pipeline::RuntimeSourceMutationResult& result) const;
    ControlResponse invalid_source_request(const std::string& message) const;
    ControlResponse pipeline_not_found(const std::string& requested_pipeline_id) const;
    ControlResponse decorate_response(ControlResponse response, const std::string& request_id,
                                      const std::string& message_type) const;
};

}  // namespace engine::infrastructure::control