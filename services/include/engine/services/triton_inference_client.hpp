#pragma once
#include "engine/core/services/iexternal_inference_client.hpp"
#include <string>

namespace engine::services {

/**
 * @brief HTTP/gRPC client for NVIDIA Triton Inference Server.
 *
 * Implements IExternalInferenceClient using Triton's REST API (HTTP endpoint)
 * for model inference. gRPC support can be added by replacing the transport
 * layer without changing the interface.
 *
 * This implementation is a stub — the actual HTTP/gRPC plumbing is filled in
 * once Triton SDK headers are available in the build environment.
 */
class TritonInferenceClient : public engine::core::services::IExternalInferenceClient {
   public:
    /**
     * @brief Construct client pointing at a Triton server endpoint.
     * @param server_url  Base URL of the Triton server,
     *                    e.g. "http://localhost:8000" or "grpc://localhost:8001"
     */
    explicit TritonInferenceClient(const std::string& server_url);
    ~TritonInferenceClient() override;

    bool connect() override;
    bool disconnect() override;

    /**
     * @brief Send an inference request to Triton.
     * @param model_name  Registered Triton model name.
     * @param input_data  Raw input buffer.
     * @param input_size  Buffer length in bytes.
     * @param result      Populated with server response on success.
     * @return true if the server accepted and returned a result.
     */
    bool infer(const std::string& model_name, const void* input_data, std::size_t input_size,
               engine::core::services::InferenceResult& result) override;

    bool is_connected() const override;

   private:
    std::string server_url_;
    bool connected_ = false;
};

}  // namespace engine::services
