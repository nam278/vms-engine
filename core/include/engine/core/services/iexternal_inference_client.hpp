#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace engine::core::services {

/**
 * @brief Result returned by a single inference call.
 */
struct InferenceResult {
    std::vector<float> outputs;  ///< raw output tensor values
    bool success = false;
};

/**
 * @brief Abstract client for an external inference server (e.g. Triton).
 *
 * Implementations connect to a remote inference service and expose a
 * synchronous infer() call. Ownership of the connection is managed
 * by the implementing class; connect() / disconnect() can be called
 * explicitly or driven by the constructor / destructor.
 */
class IExternalInferenceClient {
   public:
    virtual ~IExternalInferenceClient() = default;

    /**
     * @brief Establish connection to the inference server.
     * @return true on success.
     */
    virtual bool connect() = 0;

    /**
     * @brief Disconnect from the inference server.
     * @return true on success.
     */
    virtual bool disconnect() = 0;

    /**
     * @brief Run inference on the named model.
     * @param model_name  Model name as registered in the server.
     * @param input_data  Pointer to raw input data buffer.
     * @param input_size  Length of input buffer in bytes.
     * @param result      Output struct populated on success.
     * @return true if inference succeeded.
     */
    virtual bool infer(const std::string& model_name, const void* input_data,
                       std::size_t input_size, InferenceResult& result) = 0;

    /**
     * @brief Check whether the client currently has an active connection.
     */
    virtual bool is_connected() const = 0;
};

}  // namespace engine::core::services
