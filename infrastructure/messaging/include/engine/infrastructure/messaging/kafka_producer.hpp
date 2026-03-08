#pragma once
/**
 * @file kafka_producer.hpp
 * @brief Kafka producer implementing IMessageProducer via librdkafka C++ API.
 *
 * Uses PIMPL to avoid leaking librdkafka headers into consumers.
 *
 * Reconnect strategy
 * ------------------
 * librdkafka manages broker-level reconnects internally for an existing
 * producer handle.  If the broker goes down and comes back, librdkafka will
 * transparently reconnect and drain its internal retry queue -- no application-
 * level reconnect thread is needed.
 *
 * publish() failures (e.g. invalid topic) are logged and dropped; messages
 * that fail at the broker level are reported via the delivery-report callback.
 *
 * Thread safety: NOT thread-safe -- callers must synchronise or use separate instances.
 */
#include "engine/core/messaging/imessage_producer.hpp"

#include <memory>
#include <mutex>
#include <string>

namespace engine::infrastructure::messaging {

/**
 * @brief Kafka producer backed by librdkafka (C++ API).
 *
 * Connect flow  : connect(host, port) creates RdKafka::Producer once.
 * Publish flow  : produce() + poll(0); QUEUE_FULL triggers a 500 ms poll+retry.
 * Disconnect flow: flush(10 s) + delete producer.
 */
class KafkaProducer : public engine::core::messaging::IMessageProducer {
   public:
    KafkaProducer();
    ~KafkaProducer() override;

    // Non-copyable, non-movable
    KafkaProducer(const KafkaProducer&) = delete;
    KafkaProducer& operator=(const KafkaProducer&) = delete;

    /**
     * @brief Create the Kafka producer handle.
     * @param host    Broker hostname or IP.
     * @param port    Broker port (default Kafka: 9092).
     * @param channel Optional default topic used when publish() channel arg is empty.
     * @return true if the producer handle was created successfully.
     */
    bool connect(const std::string& host, int port, const std::string& channel = "") override;

    /** @brief Publish @p message as field "data" to @p channel (topic). */
    bool publish(const std::string& channel, const std::string& message) override;

    /**
     * @brief Publish a key-value pair to @p channel (topic).
     * @param channel Kafka topic name; falls back to default topic when empty.
     * @param key     Message key for partition routing (may be empty).
     * @param value   Message payload (copied immediately by librdkafka).
     * @return true if the message was enqueued; false on errors.
     */
    bool publish(const std::string& channel, const std::string& key,
                 const std::string& value) override;

    /**
     * @brief Publish JSON string as-is (no flattening for Kafka).
     * @param channel Kafka topic name.
     * @param json_str JSON string to publish.
     * @return true if enqueued; false on errors.
     */
    bool publish_json(const std::string& channel, const std::string& json_str) override;

    /**
     * @brief Flush in-flight messages (10 s timeout) and destroy the producer handle.
     * Safe to call multiple times.
     */
    void disconnect() override;

    /** @brief Returns true if the producer handle exists. */
    bool is_connected() const override;

   private:
    // PIMPL -- rdkafka types confined to .cpp
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::string brokers_;        ///< "host:port" kept for log messages
    std::string default_topic_;  ///< fallback topic when channel arg is empty
    mutable std::mutex mtx_;     ///< guards impl_->producer across publish threads
};

}  // namespace engine::infrastructure::messaging