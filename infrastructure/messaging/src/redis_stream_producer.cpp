/**
 * @file redis_stream_producer.cpp
 * @brief hiredis-based Redis Streams producer with non-blocking background reconnect.
 *
 * Reconnect flow
 * ──────────────
 * 1. connect()        — stores host/port; start background thread; immediate connect attempt.
 * 2. publish()        — fast atomic check; drop + schedule_reconnect() on any failure.
 * 3. reconnect_loop() — waits on cv; sleeps back-off; calls do_connect() under ctx_mtx_.
 * 4. disconnect()     — sets stop_, notifies cv, joins thread, frees ctx_.
 */
#include "engine/infrastructure/messaging/redis_stream_producer.hpp"
#include "engine/core/utils/logger.hpp"

#include <hiredis/hiredis.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>

namespace engine::infrastructure::messaging {

// ─── lifecycle ───────────────────────────────────────────────────────────────

RedisStreamProducer::~RedisStreamProducer() {
    disconnect();
}

bool RedisStreamProducer::connect(const std::string& host, int port,
                                  const std::string& /*channel*/) {
    if (reconnect_thread_.joinable()) {
        return true;  // already initialised
    }

    host_ = host;
    port_ = port;
    stop_.store(false);

    // Attempt immediate connection (background thread will retry on failure)
    {
        std::lock_guard<std::mutex> lk(ctx_mtx_);
        if (do_connect()) {
            LOG_I("RedisStreamProducer: connected to {}:{}", host_, port_);
        } else {
            LOG_W("RedisStreamProducer: initial connect to {}:{} failed — retrying in background",
                  host_, port_);
        }
    }

    reconnect_thread_ = std::thread(&RedisStreamProducer::reconnect_loop, this);
    return true;  // init always succeeds; connection is async
}

// ─── internal connect (caller must hold ctx_mtx_) ────────────────────────────

bool RedisStreamProducer::do_connect() {
    if (ctx_) {
        redisFree(ctx_);
        ctx_ = nullptr;
        connected_.store(false);
    }

    struct timeval tv {
        CONNECT_TIMEOUT_SEC, 0
    };
    ctx_ = redisConnectWithTimeout(host_.c_str(), port_, tv);
    if (!ctx_) {
        LOG_E("RedisStreamProducer: cannot allocate hiredis context");
        return false;
    }
    if (ctx_->err) {
        LOG_E("RedisStreamProducer: {}:{} — {}", host_, port_, ctx_->errstr);
        redisFree(ctx_);
        ctx_ = nullptr;
        return false;
    }

    // PING to verify liveness before marking connected
    redisReply* r = static_cast<redisReply*>(redisCommand(ctx_, "PING"));
    if (!r || r->type != REDIS_REPLY_STATUS || std::string(r->str) != "PONG") {
        if (r)
            freeReplyObject(r);
        LOG_E("RedisStreamProducer: PING to {}:{} failed", host_, port_);
        redisFree(ctx_);
        ctx_ = nullptr;
        return false;
    }
    freeReplyObject(r);

    connected_.store(true);
    return true;
}

// ─── background reconnect thread ─────────────────────────────────────────────

void RedisStreamProducer::schedule_reconnect() {
    reconnect_needed_.store(true);
    cv_.notify_one();
}

void RedisStreamProducer::reconnect_loop() {
    int backoff_sec = RECONNECT_BASE_SEC;
    while (!stop_.load()) {
        {
            std::unique_lock<std::mutex> lk(cv_mtx_);
            cv_.wait(lk, [this] { return reconnect_needed_.load() || stop_.load(); });
        }
        if (stop_.load())
            break;
        reconnect_needed_.store(false);

        if (connected_.load()) {
            backoff_sec = RECONNECT_BASE_SEC;  // already reconnected — reset backoff
            continue;
        }

        LOG_D("RedisStreamProducer: reconnect in {}s ({}:{})", backoff_sec, host_, port_);
        // Sleep in 100 ms increments so stop_ is respected quickly
        for (int i = 0; i < backoff_sec * 10 && !stop_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (stop_.load())
            break;

        {
            std::lock_guard<std::mutex> lk(ctx_mtx_);
            if (do_connect()) {
                LOG_I("RedisStreamProducer: reconnected to {}:{}", host_, port_);
                backoff_sec = RECONNECT_BASE_SEC;  // reset on success
                continue;
            }
        }

        // Failed — double backoff (capped) and queue another attempt
        backoff_sec = std::min(backoff_sec * 2, RECONNECT_MAX_SEC);
        LOG_W("RedisStreamProducer: reconnect failed, next attempt in {}s", backoff_sec);
        schedule_reconnect();
    }
}

// ─── disconnect ──────────────────────────────────────────────────────────────

void RedisStreamProducer::disconnect() {
    stop_.store(true);
    cv_.notify_all();
    if (reconnect_thread_.joinable()) {
        reconnect_thread_.join();
    }
    std::lock_guard<std::mutex> lk(ctx_mtx_);
    if (ctx_) {
        redisFree(ctx_);
        ctx_ = nullptr;
        connected_.store(false);
        LOG_D("RedisStreamProducer: disconnected from {}:{}", host_, port_);
    }
}

bool RedisStreamProducer::is_connected() const {
    return connected_.load();
}

// ─── publish ─────────────────────────────────────────────────────────────────

bool RedisStreamProducer::publish(const std::string& channel, const std::string& message) {
    return publish(channel, "data", message);
}

bool RedisStreamProducer::publish(const std::string& channel, const std::string& key,
                                  const std::string& value) {
    // Fast path — no lock needed for this check
    if (!connected_.load()) {
        LOG_D("RedisStreamProducer: not connected — dropping '{}', scheduling reconnect", channel);
        schedule_reconnect();
        return false;
    }

    std::lock_guard<std::mutex> lk(ctx_mtx_);
    if (!ctx_) {
        connected_.store(false);
        schedule_reconnect();
        return false;
    }

    // XADD <stream> MAXLEN ~ 100000 * <key> <value>
    redisReply* reply =
        static_cast<redisReply*>(redisCommand(ctx_, "XADD %s MAXLEN ~ %d * %s %s", channel.c_str(),
                                              STREAM_MAXLEN, key.c_str(), value.c_str()));

    if (!reply) {
        LOG_W("RedisStreamProducer: XADD '{}' null reply — connection lost, scheduling reconnect",
              channel);
        redisFree(ctx_);
        ctx_ = nullptr;
        connected_.store(false);
        schedule_reconnect();
        return false;
    }

    if (reply->type == REDIS_REPLY_ERROR) {
        LOG_W("RedisStreamProducer: XADD '{}' error: {} — scheduling reconnect", channel,
              reply->str);
        freeReplyObject(reply);
        connected_.store(false);
        schedule_reconnect();
        return false;
    }

    freeReplyObject(reply);
    return true;
}

// ─── publish_json (flatten JSON fields) ──────────────────────────────────────

bool RedisStreamProducer::publish_json(const std::string& channel, const std::string& json_str) {
    // Fast path — no lock needed for this check
    if (!connected_.load()) {
        LOG_D("RedisStreamProducer: not connected — dropping '{}', scheduling reconnect", channel);
        schedule_reconnect();
        return false;
    }

    // Parse JSON
    nlohmann::json obj;
    try {
        obj = nlohmann::json::parse(json_str);
    } catch (const std::exception& e) {
        LOG_E("RedisStreamProducer: JSON parse error in publish_json: {}", e.what());
        return false;
    }

    if (!obj.is_object()) {
        LOG_E("RedisStreamProducer: publish_json expects object, got {}", obj.type_name());
        return false;
    }

    // Build XADD command: XADD channel MAXLEN ~ 100000 * key1 val1 key2 val2 ...
    std::vector<const char*> argv;
    std::vector<std::string> argv_storage;  // keep strings alive

    argv_storage.push_back("XADD");
    argv_storage.push_back(channel);
    argv_storage.push_back("MAXLEN");
    argv_storage.push_back("~");
    argv_storage.push_back(std::to_string(STREAM_MAXLEN));
    argv_storage.push_back("*");

    // Flatten JSON fields
    for (auto& [key, val] : obj.items()) {
        argv_storage.push_back(key);
        if (val.is_string()) {
            argv_storage.push_back(val.get<std::string>());
        } else if (val.is_number()) {
            argv_storage.push_back(val.dump());  // e.g. "0.6"
        } else if (val.is_boolean()) {
            argv_storage.push_back(val.get<bool>() ? "true" : "false");
        } else {
            // null, array, object — dump as JSON string
            argv_storage.push_back(val.dump());
        }
    }

    // Convert storage strings to C pointers for redisCommandArgv
    for (const auto& s : argv_storage) {
        argv.push_back(s.c_str());
    }

    if (argv.size() < 6) {
        LOG_D("RedisStreamProducer: no fields to publish to '{}'", channel);
        return false;
    }

    std::lock_guard<std::mutex> lk(ctx_mtx_);
    if (!ctx_) {
        connected_.store(false);
        schedule_reconnect();
        return false;
    }

    // Send XADD command
    redisReply* reply =
        static_cast<redisReply*>(redisCommandArgv(ctx_, argv.size(), argv.data(), nullptr));

    if (!reply) {
        LOG_W("RedisStreamProducer: XADD '{}' null reply — connection lost, scheduling reconnect",
              channel);
        redisFree(ctx_);
        ctx_ = nullptr;
        connected_.store(false);
        schedule_reconnect();
        return false;
    }

    if (reply->type == REDIS_REPLY_ERROR) {
        LOG_W("RedisStreamProducer: XADD '{}' error: {} — scheduling reconnect", channel,
              reply->str);
        freeReplyObject(reply);
        connected_.store(false);
        schedule_reconnect();
        return false;
    }

    freeReplyObject(reply);
    return true;
}

}  // namespace engine::infrastructure::messaging
