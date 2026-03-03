#include "engine/services/triton_inference_client.hpp"
#include "engine/core/utils/logger.hpp"

// TODO: Replace stub implementations with real Triton HTTP/gRPC calls once
// the Triton client SDK (triton-client) is integrated into the build via
// FetchContent or system package.

namespace engine::services {

TritonInferenceClient::TritonInferenceClient(const std::string& server_url)
    : server_url_(server_url) {}

TritonInferenceClient::~TritonInferenceClient() {
    if (connected_) {
        disconnect();
    }
}

bool TritonInferenceClient::connect() {
    if (connected_) {
        LOG_W("TritonInferenceClient: already connected to '{}'", server_url_);
        return true;
    }

    LOG_I("TritonInferenceClient: connecting to '{}'", server_url_);

    // --- Stub: replace with actual HTTP health-check call ---
    // curl_easy_setopt(handle, CURLOPT_URL, (server_url_ + "/v2/health/ready").c_str());
    // ...

    connected_ = true;
    LOG_I("TritonInferenceClient: connected (stub)");
    return true;
}

bool TritonInferenceClient::disconnect() {
    if (!connected_) {
        return true;
    }

    LOG_I("TritonInferenceClient: disconnecting from '{}'", server_url_);

    // --- Stub: close HTTP session / gRPC channel ---

    connected_ = false;
    LOG_I("TritonInferenceClient: disconnected");
    return true;
}

bool TritonInferenceClient::infer(const std::string& model_name, const void* input_data,
                                  std::size_t input_size,
                                  engine::core::services::InferenceResult& result) {
    if (!connected_) {
        LOG_E("TritonInferenceClient::infer() called without active connection");
        result.success = false;
        return false;
    }

    LOG_D("TritonInferenceClient::infer(model='{}', bytes={})", model_name, input_size);

    // Suppress unused-parameter warnings until real implementation is added
    (void)input_data;

    // --- Stub: send POST /v2/models/{model_name}/infer ---
    result.outputs.clear();
    result.success = false;

    LOG_W("TritonInferenceClient::infer() stub — returning failure");
    return false;
}

bool TritonInferenceClient::is_connected() const {
    return connected_;
}

}  // namespace engine::services
