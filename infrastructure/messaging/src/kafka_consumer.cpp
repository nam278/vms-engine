/**
 * @file kafka_consumer.cpp
 * @brief Kafka consumer via librdkafka C++ API.
 */
#include "engine/infrastructure/messaging/kafka_consumer.hpp"

#include "engine/core/utils/logger.hpp"

#include <rdkafkacpp.h>

#include <algorithm>
#include <unistd.h>

namespace engine::infrastructure::messaging {

struct KafkaConsumer::Impl {
    RdKafka::KafkaConsumer* consumer = nullptr;
};

KafkaConsumer::KafkaConsumer() : impl_(std::make_unique<Impl>()) {}

KafkaConsumer::~KafkaConsumer() {
    disconnect();
}

bool KafkaConsumer::connect(const std::string& host, int port, const std::string& channel,
                            const std::string& consumer_scope) {
    std::lock_guard<std::mutex> lk(mtx_);

    if (impl_->consumer) {
        return true;
    }

    brokers_ = host + ":" + std::to_string(port);
    if (!channel.empty()) {
        default_topic_ = channel;
    }
    consumer_scope_ = consumer_scope.empty() ? std::string("default") : consumer_scope;

    std::string errstr;
    std::unique_ptr<RdKafka::Conf> conf(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
    if (conf->set("bootstrap.servers", brokers_, errstr) != RdKafka::Conf::CONF_OK) {
        LOG_E("KafkaConsumer: invalid bootstrap.servers='{}': {}", brokers_, errstr);
        return false;
    }

    const std::string group_id = consumer_scope_;
    conf->set("group.id", group_id, errstr);
    conf->set("enable.auto.commit", "true", errstr);
    conf->set("auto.offset.reset", "latest", errstr);

    impl_->consumer = RdKafka::KafkaConsumer::create(conf.get(), errstr);
    if (!impl_->consumer) {
        LOG_E("KafkaConsumer: failed to create consumer for '{}': {}", brokers_, errstr);
        return false;
    }

    LOG_I("KafkaConsumer: consumer created for {} group={} topic='{}'", brokers_, group_id,
          default_topic_);
    if (!default_topic_.empty()) {
        return subscribe(default_topic_);
    }
    return true;
}

bool KafkaConsumer::subscribe(const std::string& channel) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!impl_->consumer || channel.empty()) {
        return false;
    }

    if (std::find(subscriptions_.begin(), subscriptions_.end(), channel) == subscriptions_.end()) {
        subscriptions_.push_back(channel);
    }

    RdKafka::ErrorCode err = impl_->consumer->subscribe(subscriptions_);
    if (err != RdKafka::ERR_NO_ERROR) {
        LOG_E("KafkaConsumer: subscribe failed for '{}': {}", channel, RdKafka::err2str(err));
        return false;
    }

    LOG_I("KafkaConsumer: subscribed to {} topic(s) group={}", subscriptions_.size(),
          consumer_scope_);
    return true;
}

bool KafkaConsumer::poll(int timeout_ms, engine::core::messaging::ConsumedMessage& out_message) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!impl_->consumer) {
        return false;
    }

    std::unique_ptr<RdKafka::Message> message(impl_->consumer->consume(timeout_ms));
    if (!message) {
        return false;
    }

    switch (message->err()) {
        case RdKafka::ERR_NO_ERROR:
            out_message.channel = message->topic_name();
            out_message.id =
                std::to_string(message->partition()) + ":" + std::to_string(message->offset());
            out_message.key = message->key() ? *message->key() : "";
            out_message.payload.assign(static_cast<const char*>(message->payload()),
                                       message->len());
            return true;

        case RdKafka::ERR__TIMED_OUT:
            return false;

        default:
            LOG_W("KafkaConsumer: consume error on {}: {}", brokers_, message->errstr());
            return false;
    }
}

bool KafkaConsumer::ack(const engine::core::messaging::ConsumedMessage& /*message*/) {
    return true;
}

void KafkaConsumer::disconnect() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!impl_->consumer) {
        return;
    }

    impl_->consumer->close();
    delete impl_->consumer;
    impl_->consumer = nullptr;
    subscriptions_.clear();
    LOG_I("KafkaConsumer: disconnected from {}", brokers_);
}

bool KafkaConsumer::is_connected() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return impl_->consumer != nullptr;
}

}  // namespace engine::infrastructure::messaging