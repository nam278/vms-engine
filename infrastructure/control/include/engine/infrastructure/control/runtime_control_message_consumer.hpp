#pragma once

#include "engine/core/messaging/imessage_consumer.hpp"
#include "engine/core/messaging/imessage_producer.hpp"
#include "engine/infrastructure/control/runtime_control_handler.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace engine::infrastructure::control {

class RuntimeControlMessageConsumer {
   public:
    RuntimeControlMessageConsumer(
        engine::core::messaging::IMessageConsumer* consumer,
        engine::core::messaging::IMessageProducer* reply_producer,
        std::shared_ptr<engine::infrastructure::control::RuntimeControlHandler> handler,
        std::string channel, std::string default_reply_channel = "");
    ~RuntimeControlMessageConsumer();

    bool start();
    void stop();

   private:
    void consume_loop();

    engine::core::messaging::IMessageConsumer* consumer_;
    engine::core::messaging::IMessageProducer* reply_producer_;
    std::shared_ptr<engine::infrastructure::control::RuntimeControlHandler> handler_;
    std::string channel_;
    std::string default_reply_channel_;
    std::thread thread_;
    std::atomic<bool> stop_requested_{false};
};

}  // namespace engine::infrastructure::control