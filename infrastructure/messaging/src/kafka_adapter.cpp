/**
 * @file kafka_adapter.cpp
 * @brief Stub Kafka producer — returns false until librdkafka is linked.
 */
#include "engine/infrastructure/messaging/kafka_adapter.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::infrastructure::messaging {

KafkaAdapter::~KafkaAdapter() {
    disconnect();
}

bool KafkaAdapter::connect(const std::string& host, int port, const std::string& channel) {
    brokers_ = host + ":" + std::to_string(port);
    topic_ = channel;
    LOG_W("KafkaAdapter: Kafka support not yet implemented (brokers={})", brokers_);
    return false;
}

bool KafkaAdapter::publish(const std::string& channel, const std::string& message) {
    LOG_W("KafkaAdapter: publish() stub called — Kafka not linked");
    (void)channel;
    (void)message;
    return false;
}

bool KafkaAdapter::publish(const std::string& channel, const std::string& key,
                           const std::string& value) {
    LOG_W("KafkaAdapter: publish() stub called — Kafka not linked");
    (void)channel;
    (void)key;
    (void)value;
    return false;
}

void KafkaAdapter::disconnect() {
    connected_ = false;
}

bool KafkaAdapter::is_connected() const {
    return connected_;
}

}  // namespace engine::infrastructure::messaging
