#pragma once

#include "engine/core/messaging/imessage_consumer.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace engine::infrastructure::messaging {

/**
 * @brief Kafka consumer backed by librdkafka C++ API.
 *
 * Uses a stable consumer group derived from pipeline scope and always
 * seeks assigned partitions to their end offsets on startup/rebalance so
 * only messages produced after the current process joins are consumed.
 */
class KafkaConsumer : public engine::core::messaging::IMessageConsumer {
   public:
    KafkaConsumer();
    ~KafkaConsumer() override;

    KafkaConsumer(const KafkaConsumer&) = delete;
    KafkaConsumer& operator=(const KafkaConsumer&) = delete;

    bool connect(const std::string& host, int port, const std::string& channel = "",
                 const std::string& consumer_scope = "") override;
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
    std::string consumer_scope_;
    std::vector<std::string> subscriptions_;
    mutable std::mutex mtx_;
};

}  // namespace engine::infrastructure::messaging