#pragma once
/**
 * @file redis_stream_producer.hpp
 * @brief Redis Streams adapter implementing IMessageProducer.
 *
 * Uses hiredis for XADD to Redis streams.
 */
#include "engine/core/messaging/imessage_producer.hpp"
#include <string>
#include <memory>

// Forward-declare hiredis type to avoid leaking header
struct redisContext;

namespace engine::infrastructure::messaging {

/**
 * @brief Publishes messages to Redis Streams via XADD.
 *
 * Connection is established lazily on first publish() or explicitly via connect().
 * Thread safety: NOT thread-safe — callers must synchronize or use separate instances.
 */
class RedisStreamProducer : public engine::core::messaging::IMessageProducer {
   public:
    RedisStreamProducer() = default;
    ~RedisStreamProducer() override;

    // Non-copyable, non-movable (owns raw hiredis context)
    RedisStreamProducer(const RedisStreamProducer&) = delete;
    RedisStreamProducer& operator=(const RedisStreamProducer&) = delete;

    bool connect(const std::string& host, int port, const std::string& channel = "") override;
    bool publish(const std::string& channel, const std::string& message) override;
    bool publish(const std::string& channel, const std::string& key,
                 const std::string& value) override;
    void disconnect() override;
    bool is_connected() const override;

   private:
    std::string host_;
    int port_ = 6379;
    redisContext* ctx_ = nullptr;
};

}  // namespace engine::infrastructure::messaging
