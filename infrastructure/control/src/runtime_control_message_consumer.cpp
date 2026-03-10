#include "engine/infrastructure/control/runtime_control_message_consumer.hpp"

#include "engine/core/utils/logger.hpp"

#include <nlohmann/json.hpp>

namespace engine::infrastructure::control {

RuntimeControlMessageConsumer::RuntimeControlMessageConsumer(
    engine::core::messaging::IMessageConsumer* consumer,
    engine::core::messaging::IMessageProducer* reply_producer,
    std::shared_ptr<engine::infrastructure::control::RuntimeControlHandler> handler,
    std::string channel, std::string default_reply_channel)
    : consumer_(consumer),
      reply_producer_(reply_producer),
      handler_(std::move(handler)),
      channel_(std::move(channel)),
      default_reply_channel_(std::move(default_reply_channel)) {}

RuntimeControlMessageConsumer::~RuntimeControlMessageConsumer() {
    stop();
}

bool RuntimeControlMessageConsumer::start() {
    if (consumer_ == nullptr || handler_ == nullptr) {
        LOG_E("Control messaging: missing consumer or runtime control handler");
        return false;
    }
    if (thread_.joinable()) {
        return true;
    }
    if (!consumer_->subscribe(channel_)) {
        LOG_E("Control messaging: failed to subscribe to '{}'", channel_);
        return false;
    }

    stop_requested_.store(false);
    thread_ = std::thread(&RuntimeControlMessageConsumer::consume_loop, this);
    LOG_I("Control messaging: consuming commands from '{}'", channel_);
    return true;
}

void RuntimeControlMessageConsumer::stop() {
    stop_requested_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
}

void RuntimeControlMessageConsumer::consume_loop() {
    while (!stop_requested_.load()) {
        engine::core::messaging::ConsumedMessage message;
        if (!consumer_->poll(250, message)) {
            continue;
        }

        std::string reply_channel = default_reply_channel_;
        try {
            if (!message.payload.empty()) {
                const auto parsed = nlohmann::json::parse(message.payload);
                reply_channel = parsed.value("reply_to", reply_channel);
            }
        } catch (const std::exception&) {
        }

        const auto response = handler_->handle_message(message.payload);
        if (reply_producer_ != nullptr && !reply_channel.empty()) {
            if (!reply_producer_->publish_json(reply_channel, response.body.dump())) {
                LOG_W("Control messaging: failed to publish reply to '{}'", reply_channel);
            }
        }

        consumer_->ack(message);
    }
}

}  // namespace engine::infrastructure::control