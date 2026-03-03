#pragma once
#include <string>

namespace engine::core::messaging {

/**
 * @brief Publishes messages to an external broker (Redis Streams, Kafka, etc.).
 */
class IMessageProducer {
   public:
    virtual ~IMessageProducer() = default;
    virtual bool connect(const std::string& host, int port, const std::string& channel = "") = 0;
    virtual bool publish(const std::string& channel, const std::string& message) = 0;
    virtual bool publish(const std::string& channel, const std::string& key,
                         const std::string& value) = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;
};

}  // namespace engine::core::messaging
