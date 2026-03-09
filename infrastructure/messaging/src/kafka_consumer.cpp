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

namespace {

/**
 * @brief Rebalance callback that forces a stable group to start from latest.
 *
 * Kafka's `auto.offset.reset=latest` only applies when a group has no stored
 * offsets. This callback overrides any existing committed offsets on each fresh
 * assignment so a restarted consumer skips backlog accumulated while it was
 * offline and only consumes messages published after it rejoins the group.
 */
class SkipBacklogRebalanceCb : public RdKafka::RebalanceCb {
   public:
    void rebalance_cb(RdKafka::KafkaConsumer* consumer, RdKafka::ErrorCode err,
                      std::vector<RdKafka::TopicPartition*>& partitions) override {
        if (consumer == nullptr) {
            return;
        }

        switch (err) {
            case RdKafka::ERR__ASSIGN_PARTITIONS: {
                for (auto* partition : partitions) {
                    if (partition == nullptr) {
                        continue;
                    }

                    int64_t low = RdKafka::Topic::OFFSET_INVALID;
                    int64_t high = RdKafka::Topic::OFFSET_INVALID;
                    const auto watermark_err = consumer->query_watermark_offsets(
                        partition->topic(), partition->partition(), &low, &high, 5000);

                    if (watermark_err == RdKafka::ERR_NO_ERROR) {
                        partition->set_offset(high);
                    } else {
                        partition->set_offset(RdKafka::Topic::OFFSET_END);
                        LOG_W(
                            "KafkaConsumer: failed to query watermark for {}[{}]: {} "
                            "- falling back to OFFSET_END",
                            partition->topic(), partition->partition(),
                            RdKafka::err2str(watermark_err));
                    }
                }

                const auto assign_err = consumer->assign(partitions);
                if (assign_err != RdKafka::ERR_NO_ERROR) {
                    LOG_E("KafkaConsumer: assign latest offsets failed: {}",
                          RdKafka::err2str(assign_err));
                    return;
                }

                LOG_I("KafkaConsumer: assigned {} partition(s) at latest offsets",
                      partitions.size());
                return;
            }

            case RdKafka::ERR__REVOKE_PARTITIONS: {
                const auto unassign_err = consumer->unassign();
                if (unassign_err != RdKafka::ERR_NO_ERROR) {
                    LOG_W("KafkaConsumer: unassign on revoke failed: {}",
                          RdKafka::err2str(unassign_err));
                }
                LOG_I("KafkaConsumer: revoked {} partition(s)", partitions.size());
                return;
            }

            default: {
                const auto unassign_err = consumer->unassign();
                if (unassign_err != RdKafka::ERR_NO_ERROR) {
                    LOG_W("KafkaConsumer: unassign after rebalance error failed: {}",
                          RdKafka::err2str(unassign_err));
                }
                LOG_W("KafkaConsumer: rebalance event error: {}", RdKafka::err2str(err));
                return;
            }
        }
    }
};

}  // namespace

struct KafkaConsumer::Impl {
    RdKafka::KafkaConsumer* consumer = nullptr;
    SkipBacklogRebalanceCb rebalance_cb;
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

        std::string errstr;
        std::unique_ptr<RdKafka::Conf> conf(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
        if (conf->set("bootstrap.servers", brokers_, errstr) != RdKafka::Conf::CONF_OK) {
            LOG_E("KafkaConsumer: invalid bootstrap.servers='{}': {}", brokers_, errstr);
            return false;
        }

        const std::string group_id = consumer_scope_;
        if (conf->set("group.id", group_id, errstr) != RdKafka::Conf::CONF_OK) {
            LOG_E("KafkaConsumer: invalid group.id='{}': {}", group_id, errstr);
            return false;
        }
        if (conf->set("enable.auto.commit", "true", errstr) != RdKafka::Conf::CONF_OK) {
            LOG_E("KafkaConsumer: invalid enable.auto.commit: {}", errstr);
            return false;
        }
        if (conf->set("auto.offset.reset", "latest", errstr) != RdKafka::Conf::CONF_OK) {
            LOG_E("KafkaConsumer: invalid auto.offset.reset: {}", errstr);
            return false;
        }
        if (conf->set("rebalance_cb", &impl_->rebalance_cb, errstr) != RdKafka::Conf::CONF_OK) {
            LOG_E("KafkaConsumer: failed to install rebalance callback: {}", errstr);
            return false;
        }

        impl_->consumer = RdKafka::KafkaConsumer::create(conf.get(), errstr);
        if (!impl_->consumer) {
            LOG_E("KafkaConsumer: failed to create consumer for '{}': {}", brokers_, errstr);
            return false;
        }

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