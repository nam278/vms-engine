#pragma once
/**
 * @file redis_stream_producer.hpp
 * @brief Redis Streams adapter implementing IMessageProducer.
 *
 * Reconnect strategy
 * ──────────────────
 * • connect() attempts an immediate connection then starts a background reconnect thread.
 * • publish() is fire-and-forget: if disconnected or XADD fails, the message is
 *   dropped, a warning is logged, and the background thread is signalled to reconnect
 *   asynchronously — the pipeline thread is NEVER blocked.
 * • Background thread: waits for reconnect signal, retries with exponential back-off
 *   (5 s → 10 s → … capped at 60 s).
 * • Thread safety: connect/publish/disconnect are guarded by ctx_mtx_.
 */
#include "engine/core/messaging/imessage_producer.hpp"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

// Forward-declare hiredis type to avoid leaking header
struct redisContext;

namespace engine::infrastructure::messaging {

/**
 * @brief Publishes messages to Redis Streams via XADD with automatic background reconnect.
 *
 * Usage:
 *   producer.connect("localhost", 6379);  // starts background thread
 *   producer.publish("my-stream", "data", payload);  // fire-and-forget
 *   producer.disconnect();                // stops thread, frees context
 */
class RedisStreamProducer : public engine::core::messaging::IMessageProducer {
   public:
    RedisStreamProducer() = default;
    ~RedisStreamProducer() override;

    // Non-copyable, non-movable
    RedisStreamProducer(const RedisStreamProducer&) = delete;
    RedisStreamProducer& operator=(const RedisStreamProducer&) = delete;

    /**
     * @brief Store connection parameters, attempt immediate connect, start reconnect thread.
     * @return Always true — initial failure is handled transparently by background thread.
     */
    bool connect(const std::string& host, int port, const std::string& channel = "") override;

    /** @brief Publish @p message as field "data" to @p channel stream. Fire-and-forget. */
    bool publish(const std::string& channel, const std::string& message) override;

    /**
     * @brief Publish a key-value field to @p channel (Redis Stream name).
     * @return true on success; false if not connected or XADD fails (non-fatal, logged).
     */
    bool publish(const std::string& channel, const std::string& key,
                 const std::string& value) override;

    /**
     * @brief Publish a JSON object by flattening all fields directly into Redis Stream.
     * Each key-value pair becomes a separate field in XADD (no wrapper).
     * All values are converted to strings for Redis.
     * Non-object JSON (primitives, arrays) are logged as error and skipped.
     *
     * Example:
     *   {"class": "bike", "conf": 0.6, "left": 100}
     *   → XADD channel * class bike conf "0.6" left "100"
     *
     * @return true if XADD succeeded; false if not connected or no flat fields.
     */
    bool publish_json(const std::string& channel, const std::string& json_str);

    /** @brief Stop reconnect thread and release hiredis context. */
    void disconnect() override;

    bool is_connected() const override;

   private:
    /** @brief Attempt the actual hiredis connect + PING. Caller must hold ctx_mtx_. */
    bool do_connect();

    /** @brief Signal the background thread that a reconnect is needed. */
    void schedule_reconnect();

    /** @brief Background thread body — retries do_connect() with exponential back-off. */
    void reconnect_loop();

    // ── connection state ──────────────────────────────────────────────────────
    std::string host_;
    int port_ = 6379;
    redisContext* ctx_ = nullptr;
    std::mutex ctx_mtx_;  ///< guards ctx_ and hiredis API calls (not thread-safe)
    std::atomic<bool> connected_{false};

    // ── background reconnect thread ───────────────────────────────────────────
    std::thread reconnect_thread_;
    std::mutex cv_mtx_;
    std::condition_variable cv_;
    std::atomic<bool> reconnect_needed_{false};
    std::atomic<bool> stop_{false};

    static constexpr int CONNECT_TIMEOUT_SEC = 2;
    static constexpr int RECONNECT_BASE_SEC = 5;
    static constexpr int RECONNECT_MAX_SEC = 60;
};

}  // namespace engine::infrastructure::messaging
