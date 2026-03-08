/**
 * @file redis_stream_consumer.cpp
 * @brief hiredis-based Redis Streams consumer using XREAD.
 */
#include "engine/infrastructure/messaging/redis_stream_consumer.hpp"

#include "engine/core/utils/logger.hpp"

#include <hiredis/hiredis.h>
#include <nlohmann/json.hpp>

#include <algorithm>

namespace engine::infrastructure::messaging {

RedisStreamConsumer::~RedisStreamConsumer() {
    disconnect();
}

bool RedisStreamConsumer::connect(const std::string& host, int port, const std::string& channel) {
    host_ = host;
    port_ = port;

    std::lock_guard<std::mutex> lk(ctx_mtx_);
    if (!do_connect()) {
        return false;
    }

    if (!channel.empty()) {
        return subscribe(channel);
    }
    return true;
}

bool RedisStreamConsumer::do_connect() {
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
        LOG_E("RedisStreamConsumer: cannot allocate hiredis context");
        return false;
    }
    if (ctx_->err) {
        LOG_E("RedisStreamConsumer: {}:{} — {}", host_, port_, ctx_->errstr);
        redisFree(ctx_);
        ctx_ = nullptr;
        return false;
    }

    redisReply* reply = static_cast<redisReply*>(redisCommand(ctx_, "PING"));
    if (!reply || reply->type != REDIS_REPLY_STATUS || std::string(reply->str) != "PONG") {
        if (reply) {
            freeReplyObject(reply);
        }
        LOG_E("RedisStreamConsumer: PING to {}:{} failed", host_, port_);
        redisFree(ctx_);
        ctx_ = nullptr;
        return false;
    }
    freeReplyObject(reply);
    connected_.store(true);
    LOG_I("RedisStreamConsumer: connected to {}:{}", host_, port_);
    return true;
}

bool RedisStreamConsumer::subscribe(const std::string& channel) {
    if (channel.empty()) {
        return false;
    }

    if (std::find(channels_.begin(), channels_.end(), channel) == channels_.end()) {
        channels_.push_back(channel);
        last_ids_[channel] = "$";
        LOG_I("RedisStreamConsumer: subscribed to stream '{}'", channel);
    }
    return true;
}

bool RedisStreamConsumer::poll(int timeout_ms,
                               engine::core::messaging::ConsumedMessage& out_message) {
    if (!connected_.load() || channels_.empty()) {
        return false;
    }

    std::vector<std::string> argv_storage;
    argv_storage.reserve(5 + (channels_.size() * 2));
    argv_storage.push_back("XREAD");
    argv_storage.push_back("BLOCK");
    argv_storage.push_back(std::to_string(std::max(timeout_ms, 0)));
    argv_storage.push_back("COUNT");
    argv_storage.push_back("1");
    argv_storage.push_back("STREAMS");
    for (const auto& channel : channels_) {
        argv_storage.push_back(channel);
    }
    for (const auto& channel : channels_) {
        argv_storage.push_back(last_ids_[channel]);
    }

    std::vector<const char*> argv;
    std::vector<size_t> argvlen;
    argv.reserve(argv_storage.size());
    argvlen.reserve(argv_storage.size());
    for (const auto& item : argv_storage) {
        argv.push_back(item.c_str());
        argvlen.push_back(item.size());
    }

    std::lock_guard<std::mutex> lk(ctx_mtx_);
    if (!ctx_) {
        connected_.store(false);
        return false;
    }

    redisReply* reply =
        static_cast<redisReply*>(redisCommandArgv(ctx_, argv.size(), argv.data(), argvlen.data()));
    if (!reply) {
        LOG_W("RedisStreamConsumer: XREAD returned null reply");
        connected_.store(false);
        return false;
    }

    if (reply->type == REDIS_REPLY_NIL || reply->elements == 0) {
        freeReplyObject(reply);
        return false;
    }

    if (reply->type != REDIS_REPLY_ARRAY) {
        LOG_W("RedisStreamConsumer: unexpected reply type {}", reply->type);
        freeReplyObject(reply);
        return false;
    }

    redisReply* stream_reply = reply->element[0];
    if (!stream_reply || stream_reply->type != REDIS_REPLY_ARRAY || stream_reply->elements < 2) {
        freeReplyObject(reply);
        return false;
    }

    redisReply* stream_name = stream_reply->element[0];
    redisReply* stream_entries = stream_reply->element[1];
    if (!stream_name || !stream_entries || stream_entries->type != REDIS_REPLY_ARRAY ||
        stream_entries->elements == 0) {
        freeReplyObject(reply);
        return false;
    }

    redisReply* entry = stream_entries->element[0];
    if (!entry || entry->type != REDIS_REPLY_ARRAY || entry->elements < 2) {
        freeReplyObject(reply);
        return false;
    }

    redisReply* entry_id = entry->element[0];
    redisReply* fields = entry->element[1];
    if (!entry_id || !fields || fields->type != REDIS_REPLY_ARRAY) {
        freeReplyObject(reply);
        return false;
    }

    nlohmann::json payload = nlohmann::json::object();
    for (size_t index = 0; index + 1 < fields->elements; index += 2) {
        auto* key = fields->element[index];
        auto* value = fields->element[index + 1];
        if (!key || !value || !key->str) {
            continue;
        }
        payload[key->str] = value->str ? value->str : "";
    }

    out_message.channel = stream_name->str ? stream_name->str : "";
    out_message.id = entry_id->str ? entry_id->str : "";
    out_message.key.clear();
    out_message.payload = payload.dump();
    last_ids_[out_message.channel] = out_message.id;

    freeReplyObject(reply);
    return true;
}

bool RedisStreamConsumer::ack(const engine::core::messaging::ConsumedMessage& /*message*/) {
    return true;
}

void RedisStreamConsumer::disconnect() {
    std::lock_guard<std::mutex> lk(ctx_mtx_);
    if (ctx_) {
        redisFree(ctx_);
        ctx_ = nullptr;
    }
    connected_.store(false);
}

bool RedisStreamConsumer::is_connected() const {
    return connected_.load();
}

}  // namespace engine::infrastructure::messaging