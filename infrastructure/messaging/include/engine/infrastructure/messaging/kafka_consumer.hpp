#pragma once

#include "engine/core/messaging/imessage_consumer.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace engine::infrastructure::messaging {

/**
 * @brief Kafka consumer backed by librdkafka C++ API.
 */
class KafkaConsumer : public engine::core::messaging::IMessageConsumer {
   public:
    KafkaConsumer();
    ~KafkaConsumer() override;

    KafkaConsumer(const KafkaConsumer&) = delete;
    KafkaConsumer& operator=(const KafkaConsumer&) = delete;

    bool connect(const std::string& host, int port, const std::string& channel = "") override;
    bool subscribe(const std::string& channel) override;
    bool poll(int timeout_ms, engine::core::messaging::ConsumedMessage& out_message) override;
    bool ack(const engine::core::messaging::ConsumedMessage& message) override;
    void disconnect() override;
    bool is_connected() const override;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::string brokers_;
    std::string default_topic_;
    std::vector<std::string> subscriptions_;
    mutable std::mutex mtx_;
};

}  // namespace engine::infrastructure::messaging