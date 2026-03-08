/**
 * @file kafka_producer.cpp
 * @brief Kafka producer via librdkafka C++ API.
 *
 * Reconnect strategy
 * ------------------
 * librdkafka manages broker-level TCP reconnects internally for an existing
 * RdKafka::Producer handle.  When the broker is unreachable, librdkafka will
 * keep retrying delivery for up to `message.timeout.ms` and will reconnect
 * transparently when the broker comes back -- no application thread needed.
 *
 * See: https://github.com/confluentinc/librdkafka/blob/master/INTRODUCTION.md
 *      (sections "Producer reconnects" and "Message reliability")
 */
#include "engine/infrastructure/messaging/kafka_producer.hpp"
#include "engine/core/utils/logger.hpp"

#include <rdkafkacpp.h>

namespace engine::infrastructure::messaging {

struct KafkaProducer::Impl : public RdKafka::DeliveryReportCb {
    RdKafka::Producer* producer = nullptr;

    void dr_cb(RdKafka::Message& msg) override {
        if (msg.err()) {
            LOG_W("KafkaProducer: delivery failed -- topic={} err={}", msg.topic_name(),
                  msg.errstr());
        } else {
            LOG_D("KafkaProducer: delivered -- topic={} partition={} offset={}", msg.topic_name(),
                  msg.partition(), msg.offset());
        }
    }
};

KafkaProducer::KafkaProducer() : impl_(std::make_unique<Impl>()) {}

KafkaProducer::~KafkaProducer() {
    disconnect();
}

bool KafkaProducer::connect(const std::string& host, int port, const std::string& channel) {
    std::lock_guard<std::mutex> lk(mtx_);

    if (impl_->producer) {
        LOG_D("KafkaProducer: already connected to {}", brokers_);
        return true;
    }

    brokers_ = host + ":" + std::to_string(port);
    if (!channel.empty()) {
        default_topic_ = channel;
    }

    std::string errstr;
    std::unique_ptr<RdKafka::Conf> conf(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));

    if (conf->set("bootstrap.servers", brokers_, errstr) != RdKafka::Conf::CONF_OK) {
        LOG_E("KafkaProducer: invalid bootstrap.servers='{}': {}", brokers_, errstr);
        return false;
    }

    // Once a message is accepted by librdkafka, let the library own reconnect and retry policy.
    conf->set("message.timeout.ms", "0", errstr);
    conf->set("request.required.acks", "1", errstr);
    conf->set("message.send.max.retries", "2147483647", errstr);

    conf->set("reconnect.backoff.ms", "5000", errstr);
    conf->set("reconnect.backoff.max.ms", "60000", errstr);

    if (conf->set("dr_cb", static_cast<RdKafka::DeliveryReportCb*>(impl_.get()), errstr) !=
        RdKafka::Conf::CONF_OK) {
        LOG_E("KafkaProducer: failed to set dr_cb: {}", errstr);
        return false;
    }

    impl_->producer = RdKafka::Producer::create(conf.get(), errstr);
    if (!impl_->producer) {
        LOG_E("KafkaProducer: failed to create producer for '{}': {}", brokers_, errstr);
        return false;
    }

    LOG_I("KafkaProducer: producer created for {} (librdkafka handles broker reconnects)",
          brokers_);
    return true;
}

bool KafkaProducer::publish(const std::string& channel, const std::string& message) {
    return publish(channel, "", message);
}

bool KafkaProducer::publish(const std::string& channel, const std::string& key,
                            const std::string& value) {
    std::lock_guard<std::mutex> lk(mtx_);

    if (!impl_->producer) {
        LOG_W("KafkaProducer: not connected -- dropping publish to '{}'", channel);
        return false;
    }

    const std::string topic = channel.empty() ? default_topic_ : channel;
    if (topic.empty()) {
        LOG_E("KafkaProducer: publish() called with no topic and no default_topic");
        return false;
    }

retry:
    RdKafka::ErrorCode err = impl_->producer->produce(
        topic, RdKafka::Topic::PARTITION_UA, RdKafka::Producer::RK_MSG_COPY,
        const_cast<char*>(value.data()), value.size(), key.empty() ? nullptr : key.data(),
        key.size(), 0, nullptr, nullptr);

    if (err == RdKafka::ERR__QUEUE_FULL) {
        LOG_D("KafkaProducer: internal queue full -- polling 500 ms (topic='{}')", topic);
        impl_->producer->poll(500);
        goto retry;
    }

    if (err != RdKafka::ERR_NO_ERROR) {
        LOG_W("KafkaProducer: produce enqueue error -- topic='{}' err={}", topic,
              RdKafka::err2str(err));
        return false;
    }

    impl_->producer->poll(0);
    return true;
}

bool KafkaProducer::publish_json(const std::string& channel, const std::string& json_str) {
    return publish(channel, "", json_str);
}

void KafkaProducer::disconnect() {
    std::lock_guard<std::mutex> lk(mtx_);

    if (!impl_->producer) {
        return;
    }

    LOG_D("KafkaProducer: flushing {} in-flight message(s) to {}...", impl_->producer->outq_len(),
          brokers_);
    impl_->producer->flush(10 * 1000);

    if (impl_->producer->outq_len() > 0) {
        LOG_W("KafkaProducer: {} message(s) not delivered after flush -- dropping",
              impl_->producer->outq_len());
    }

    delete impl_->producer;
    impl_->producer = nullptr;

    LOG_I("KafkaProducer: disconnected from {}", brokers_);
}

bool KafkaProducer::is_connected() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return impl_->producer != nullptr;
}

}  // namespace engine::infrastructure::messaging