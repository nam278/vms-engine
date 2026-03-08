#pragma once

#include "engine/core/messaging/imessage_consumer.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct redisContext;

namespace engine::infrastructure::messaging {

/**
 * @brief Minimal Redis Streams consumer based on XREAD.
 *
 * Reads new entries only by default (`$`) and converts flat Redis field/value
 * pairs back into a JSON object payload string.
 */
class RedisStreamConsumer : public engine::core::messaging::IMessageConsumer {
   public:
    RedisStreamConsumer() = default;
    ~RedisStreamConsumer() override;

    RedisStreamConsumer(const RedisStreamConsumer&) = delete;
    RedisStreamConsumer& operator=(const RedisStreamConsumer&) = delete;

    bool connect(const std::string& host, int port, const std::string& channel = "") override;
    bool subscribe(const std::string& channel) override;
    bool poll(int timeout_ms, engine::core::messaging::ConsumedMessage& out_message) override;
    bool ack(const engine::core::messaging::ConsumedMessage& message) override;
    void disconnect() override;
    bool is_connected() const override;

   private:
    bool do_connect();

    std::string host_;
    int port_ = 6379;
    redisContext* ctx_ = nullptr;
    std::mutex ctx_mtx_;
    std::atomic<bool> connected_{false};
    std::vector<std::string> channels_;
    std::unordered_map<std::string, std::string> last_ids_;

    static constexpr int CONNECT_TIMEOUT_SEC = 2;
};

}  // namespace engine::infrastructure::messaging