/**
 * @file kafka_producer.cpp
 * @brief Kafka producer via system librdkafka C API.
 *
 * Reconnect strategy
 * ------------------
 * librdkafka manages broker-level TCP reconnects internally for an existing
 * producer handle. When the broker is unreachable, librdkafka will
 * keep retrying delivery for up to `message.timeout.ms` and will reconnect
 * transparently when the broker comes back -- no application thread needed.
 *
 * See: https://github.com/confluentinc/librdkafka/blob/master/INTRODUCTION.md
 *      (sections "Producer reconnects" and "Message reliability")
 */
#include "engine/infrastructure/messaging/kafka_producer.hpp"
#include "engine/core/utils/logger.hpp"

#include <librdkafka/rdkafka.h>

#include <array>

namespace engine::infrastructure::messaging {

namespace {

constexpr std::size_t kErrBufSize = 512U;

bool set_kafka_conf(rd_kafka_conf_t* conf, const char* key, const std::string& value) {
    std::array<char, kErrBufSize> errstr{};
    const rd_kafka_conf_res_t result =
        rd_kafka_conf_set(conf, key, value.c_str(), errstr.data(), errstr.size());
    if (result != RD_KAFKA_CONF_OK) {
        LOG_E("KafkaProducer: invalid {}='{}': {}", key, value, errstr.data());
        return false;
    }
    return true;
}

void on_delivery_report(rd_kafka_t* /*producer*/, const rd_kafka_message_t* message,
                        void* /*opaque*/) {
    const char* topic_name = message != nullptr && message->rkt != nullptr
                                 ? rd_kafka_topic_name(message->rkt)
                                 : "<unknown>";
    if (message == nullptr) {
        LOG_W("KafkaProducer: delivery callback received null message");
        return;
    }

    if (message->err != RD_KAFKA_RESP_ERR_NO_ERROR) {
        LOG_W("KafkaProducer: delivery failed -- topic={} err={}", topic_name,
              rd_kafka_err2str(message->err));
        return;
    }

    LOG_D("KafkaProducer: delivered -- topic={} partition={} offset={}", topic_name,
          message->partition, message->offset);
}

}  // namespace

struct KafkaProducer::Impl {
    rd_kafka_t* producer = nullptr;
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

    rd_kafka_conf_t* conf = rd_kafka_conf_new();
    if (conf == nullptr) {
        LOG_E("KafkaProducer: failed to allocate rd_kafka_conf");
        return false;
    }

    if (!set_kafka_conf(conf, "bootstrap.servers", brokers_) ||
        !set_kafka_conf(conf, "message.timeout.ms", "0") ||
        !set_kafka_conf(conf, "request.required.acks", "1") ||
        !set_kafka_conf(conf, "message.send.max.retries", "2147483647") ||
        !set_kafka_conf(conf, "reconnect.backoff.ms", "5000") ||
        !set_kafka_conf(conf, "reconnect.backoff.max.ms", "60000")) {
        rd_kafka_conf_destroy(conf);
        return false;
    }

    rd_kafka_conf_set_dr_msg_cb(conf, on_delivery_report);
    rd_kafka_conf_set_opaque(conf, impl_.get());

    std::array<char, kErrBufSize> errstr{};
    impl_->producer = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr.data(), errstr.size());
    if (!impl_->producer) {
        rd_kafka_conf_destroy(conf);
        LOG_E("KafkaProducer: failed to create producer for '{}': {}", brokers_, errstr.data());
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
    const rd_kafka_resp_err_t err = rd_kafka_producev(
        impl_->producer, RD_KAFKA_V_TOPIC(topic.c_str()),
        RD_KAFKA_V_PARTITION(RD_KAFKA_PARTITION_UA), RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
        RD_KAFKA_V_VALUE(const_cast<char*>(value.data()), value.size()),
        RD_KAFKA_V_KEY(key.empty() ? nullptr : const_cast<char*>(key.data()), key.size()),
        RD_KAFKA_V_END);

    if (err == RD_KAFKA_RESP_ERR__QUEUE_FULL) {
        LOG_D("KafkaProducer: internal queue full -- polling 500 ms (topic='{}')", topic);
        rd_kafka_poll(impl_->producer, 500);
        goto retry;
    }

    if (err != RD_KAFKA_RESP_ERR_NO_ERROR) {
        LOG_W("KafkaProducer: produce enqueue error -- topic='{}' err={}", topic,
              rd_kafka_err2str(err));
        return false;
    }

    rd_kafka_poll(impl_->producer, 0);
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

    LOG_D("KafkaProducer: flushing {} in-flight message(s) to {}...",
          rd_kafka_outq_len(impl_->producer), brokers_);
    rd_kafka_flush(impl_->producer, 10 * 1000);

    if (rd_kafka_outq_len(impl_->producer) > 0) {
        LOG_W("KafkaProducer: {} message(s) not delivered after flush -- dropping",
              rd_kafka_outq_len(impl_->producer));
    }

    rd_kafka_destroy(impl_->producer);
    impl_->producer = nullptr;

    LOG_I("KafkaProducer: disconnected from {}", brokers_);
}

bool KafkaProducer::is_connected() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return impl_->producer != nullptr;
}

}  // namespace engine::infrastructure::messaging