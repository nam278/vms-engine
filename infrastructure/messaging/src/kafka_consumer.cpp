/**
 * @file kafka_consumer.cpp
 * @brief Kafka consumer via system librdkafka C API.
 */
#include "engine/infrastructure/messaging/kafka_consumer.hpp"

#include "engine/core/utils/logger.hpp"

#include <librdkafka/rdkafka.h>

#include <algorithm>
#include <array>

namespace engine::infrastructure::messaging {

namespace {

constexpr std::size_t kErrBufSize = 512U;

bool set_kafka_conf(rd_kafka_conf_t* conf, const char* key, const std::string& value) {
    std::array<char, kErrBufSize> errstr{};
    const rd_kafka_conf_res_t result =
        rd_kafka_conf_set(conf, key, value.c_str(), errstr.data(), errstr.size());
    if (result != RD_KAFKA_CONF_OK) {
        LOG_E("KafkaConsumer: invalid {}='{}': {}", key, value, errstr.data());
        return false;
    }
    return true;
}

/**
 * @brief Rebalance callback that forces a stable group to start from latest.
 *
 * Kafka's `auto.offset.reset=latest` only applies when a group has no stored
 * offsets. This callback overrides any existing committed offsets on each fresh
 * assignment so a restarted consumer skips backlog accumulated while it was
 * offline and only consumes messages published after it rejoins the group.
 */
void on_rebalance(rd_kafka_t* consumer, rd_kafka_resp_err_t err,
                  rd_kafka_topic_partition_list_t* partitions, void* /*opaque*/) {
    if (consumer == nullptr) {
        return;
    }

    switch (err) {
        case RD_KAFKA_RESP_ERR__ASSIGN_PARTITIONS: {
            for (int index = 0; partitions != nullptr && index < partitions->cnt; ++index) {
                partitions->elems[index].offset = RD_KAFKA_OFFSET_END;
            }

            const rd_kafka_resp_err_t assign_err = rd_kafka_assign(consumer, partitions);
            if (assign_err != RD_KAFKA_RESP_ERR_NO_ERROR) {
                LOG_E("KafkaConsumer: assign latest offsets failed: {}",
                      rd_kafka_err2str(assign_err));
                return;
            }

            LOG_I("KafkaConsumer: assigned {} partition(s) at latest offsets",
                  partitions != nullptr ? partitions->cnt : 0);
            return;
        }

        case RD_KAFKA_RESP_ERR__REVOKE_PARTITIONS: {
            const rd_kafka_resp_err_t unassign_err = rd_kafka_assign(consumer, nullptr);
            if (unassign_err != RD_KAFKA_RESP_ERR_NO_ERROR) {
                LOG_W("KafkaConsumer: unassign on revoke failed: {}",
                      rd_kafka_err2str(unassign_err));
            }
            LOG_I("KafkaConsumer: revoked {} partition(s)",
                  partitions != nullptr ? partitions->cnt : 0);
            return;
        }

        default: {
            const rd_kafka_resp_err_t unassign_err = rd_kafka_assign(consumer, nullptr);
            if (unassign_err != RD_KAFKA_RESP_ERR_NO_ERROR) {
                LOG_W("KafkaConsumer: unassign after rebalance error failed: {}",
                      rd_kafka_err2str(unassign_err));
            }
            LOG_W("KafkaConsumer: rebalance event error: {}", rd_kafka_err2str(err));
            return;
        }
    }
}

}  // namespace

struct KafkaConsumer::Impl {
    rd_kafka_t* consumer = nullptr;
};

KafkaConsumer::KafkaConsumer() : impl_(std::make_unique<Impl>()) {}

KafkaConsumer::~KafkaConsumer() {
    disconnect();
}

bool KafkaConsumer::connect(const std::string& host, int port, const std::string& channel,
                            const std::string& consumer_scope) {
    std::string topic_to_subscribe;
    {
        std::lock_guard<std::mutex> lk(mtx_);

        if (impl_->consumer) {
            return true;
        }

        brokers_ = host + ":" + std::to_string(port);
        if (!channel.empty()) {
            default_topic_ = channel;
        }
        consumer_scope_ = consumer_scope.empty() ? std::string("default") : consumer_scope;

        rd_kafka_conf_t* conf = rd_kafka_conf_new();
        if (conf == nullptr) {
            LOG_E("KafkaConsumer: failed to allocate rd_kafka_conf");
            return false;
        }

        const std::string group_id = consumer_scope_;
        if (!set_kafka_conf(conf, "bootstrap.servers", brokers_) ||
            !set_kafka_conf(conf, "group.id", group_id) ||
            !set_kafka_conf(conf, "enable.auto.commit", "true") ||
            !set_kafka_conf(conf, "auto.offset.reset", "latest")) {
            rd_kafka_conf_destroy(conf);
            return false;
        }

        rd_kafka_conf_set_rebalance_cb(conf, on_rebalance);
        rd_kafka_conf_set_opaque(conf, impl_.get());

        std::array<char, kErrBufSize> errstr{};
        impl_->consumer = rd_kafka_new(RD_KAFKA_CONSUMER, conf, errstr.data(), errstr.size());
        if (!impl_->consumer) {
            rd_kafka_conf_destroy(conf);
            LOG_E("KafkaConsumer: failed to create consumer for '{}': {}", brokers_, errstr.data());
            return false;
        }

        rd_kafka_poll_set_consumer(impl_->consumer);

        LOG_I(
            "KafkaConsumer: consumer created for {} group={} topic='{}' (skip backlog on "
            "startup enabled)",
            brokers_, group_id, default_topic_);
        topic_to_subscribe = default_topic_;
    }

    if (!topic_to_subscribe.empty()) {
        return subscribe(topic_to_subscribe);
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

    rd_kafka_topic_partition_list_t* topics =
        rd_kafka_topic_partition_list_new(static_cast<int>(subscriptions_.size()));
    for (const auto& subscription : subscriptions_) {
        rd_kafka_topic_partition_list_add(topics, subscription.c_str(), RD_KAFKA_PARTITION_UA);
    }

    const rd_kafka_resp_err_t err = rd_kafka_subscribe(impl_->consumer, topics);
    rd_kafka_topic_partition_list_destroy(topics);
    if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
        LOG_E("KafkaConsumer: subscribe failed for '{}': {}", channel, rd_kafka_err2str(err));
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

    rd_kafka_message_t* message = rd_kafka_consumer_poll(impl_->consumer, timeout_ms);
    if (message == nullptr) {
        return false;
    }

    switch (message->err) {
        case RD_KAFKA_RESP_ERR_NO_ERROR:
            out_message.channel = message->rkt != nullptr ? rd_kafka_topic_name(message->rkt) : "";
            out_message.id =
                std::to_string(message->partition) + ":" + std::to_string(message->offset);
            out_message.key.assign(
                message->key != nullptr ? static_cast<const char*>(message->key) : "",
                message->key_len);
            out_message.payload.assign(
                message->payload != nullptr ? static_cast<const char*>(message->payload) : "",
                message->len);
            rd_kafka_message_destroy(message);
            return true;

        case RD_KAFKA_RESP_ERR__TIMED_OUT:
        case RD_KAFKA_RESP_ERR__PARTITION_EOF:
            rd_kafka_message_destroy(message);
            return false;

        default:
            LOG_W("KafkaConsumer: consume error on {}: {}", brokers_,
                  rd_kafka_message_errstr(message));
            rd_kafka_message_destroy(message);
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

    rd_kafka_consumer_close(impl_->consumer);
    rd_kafka_destroy(impl_->consumer);
    impl_->consumer = nullptr;
    subscriptions_.clear();
    LOG_I("KafkaConsumer: disconnected from {}", brokers_);
}

bool KafkaConsumer::is_connected() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return impl_->consumer != nullptr;
}

}  // namespace engine::infrastructure::messaging