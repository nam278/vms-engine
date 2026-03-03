/**
 * @file redis_stream_producer.cpp
 * @brief hiredis-based Redis Streams producer using XADD.
 */
#include "engine/infrastructure/messaging/redis_stream_producer.hpp"
#include "engine/core/utils/logger.hpp"

#include <hiredis/hiredis.h>

namespace engine::infrastructure::messaging {

RedisStreamProducer::~RedisStreamProducer() {
    disconnect();
}

bool RedisStreamProducer::connect(const std::string& host, int port,
                                  const std::string& /*channel*/) {
    if (ctx_)
        return true;  // already connected

    host_ = host;
    port_ = port;

    ctx_ = redisConnect(host_.c_str(), port_);
    if (!ctx_) {
        LOG_E("RedisStreamProducer: failed to allocate hiredis context");
        return false;
    }
    if (ctx_->err) {
        LOG_E("RedisStreamProducer: connection error to {}:{} — {}", host_, port_, ctx_->errstr);
        redisFree(ctx_);
        ctx_ = nullptr;
        return false;
    }

    LOG_I("RedisStreamProducer: connected to {}:{}", host_, port_);
    return true;
}

bool RedisStreamProducer::publish(const std::string& channel, const std::string& message) {
    return publish(channel, "data", message);
}

bool RedisStreamProducer::publish(const std::string& channel, const std::string& key,
                                  const std::string& value) {
    if (!ctx_ && !connect(host_, port_))
        return false;

    // XADD <stream> * <key> <value>
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "XADD %s * %s %s", channel.c_str(), key.c_str(), value.c_str()));

    if (!reply) {
        LOG_E("RedisStreamProducer: XADD failed (null reply), reconnecting");
        disconnect();
        return false;
    }

    bool ok = (reply->type != REDIS_REPLY_ERROR);
    if (!ok) {
        LOG_E("RedisStreamProducer: XADD error: {}", reply->str);
    }

    freeReplyObject(reply);
    return ok;
}

void RedisStreamProducer::disconnect() {
    if (ctx_) {
        redisFree(ctx_);
        ctx_ = nullptr;
        LOG_D("RedisStreamProducer: disconnected from {}:{}", host_, port_);
    }
}

bool RedisStreamProducer::is_connected() const {
    return ctx_ != nullptr;
}

}  // namespace engine::infrastructure::messaging
