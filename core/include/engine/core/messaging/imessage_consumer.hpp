#pragma once

#include <string>

namespace engine::core::messaging {

/**
 * @brief A broker message consumed from Redis Streams, Kafka, or similar systems.
 */
struct ConsumedMessage {
    std::string channel;
    std::string id;
    std::string key;
    std::string payload;
};

/**
 * @brief Consumes messages from an external broker (Redis Streams, Kafka, etc.).
 */
class IMessageConsumer {
   public:
    virtual ~IMessageConsumer() = default;

    virtual bool connect(const std::string& host, int port, const std::string& channel = "") = 0;
    virtual bool subscribe(const std::string& channel) = 0;
    virtual bool poll(int timeout_ms, ConsumedMessage& out_message) = 0;
    virtual bool ack(const ConsumedMessage& message) = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;
};

}  // namespace engine::core::messaging