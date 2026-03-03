#pragma once
/**
 * @file kafka_adapter.hpp
 * @brief Kafka adapter implementing IMessageProducer (stub).
 *
 * Placeholder for future Kafka integration via librdkafka.
 * Currently logs a warning and returns false on all operations.
 */
#include "engine/core/messaging/imessage_producer.hpp"
#include <string>

namespace engine::infrastructure::messaging {

/**
 * @brief Stub Kafka producer — logs warnings until librdkafka is linked.
 *
 * To enable: link rdkafka in CMakeLists.txt and implement the methods.
 */
class KafkaAdapter : public engine::core::messaging::IMessageProducer {
   public:
    KafkaAdapter() = default;
    ~KafkaAdapter() override;

    bool connect(const std::string& host, int port, const std::string& channel = "") override;
    bool publish(const std::string& channel, const std::string& message) override;
    bool publish(const std::string& channel, const std::string& key,
                 const std::string& value) override;
    void disconnect() override;
    bool is_connected() const override;

   private:
    std::string brokers_;
    std::string topic_;
    bool connected_ = false;
};

}  // namespace engine::infrastructure::messaging
