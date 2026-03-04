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
    /**
     * @brief Publish JSON by flattening all fields directly (no "data" wrapper).
     * Default implementation delegates to publish(channel, message) for backward compatibility.
     * Redis implementation flattens JSON fields into stream; Kafka implementation wraps as message.
     */
    virtual bool publish_json(const std::string& channel, const std::string& json_str) {
        return publish(channel, json_str);  // default: fallback to regular publish
    }
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;
};

}  // namespace engine::core::messaging
