#pragma once

#include "engine/core/config/config_types.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace engine::core::messaging {
class IMessageProducer;
}

namespace engine::pipeline::evidence {
class FrameEvidenceCache;
}

namespace engine::pipeline::extproc {

/** @brief One queued ext-proc request derived from an emitted frame_events object. */
struct FrameEventsExtProcJob {
    std::string handler_id;
    std::string schema_version = "1.0";
    std::string pipeline_id;
    int source_id = 0;
    std::string source_name;
    std::string frame_key;
    int64_t frame_ts_ms = 0;
    std::string overview_ref;
    std::string crop_ref;
    std::string object_key;
    std::string instance_key;
    uint64_t object_id = 0;
    uint64_t tracker_id = 0;
    int class_id = 0;
    std::string object_type;
    double confidence = 0.0;
    bool has_bbox = false;
    float left = 0.0F;
    float top = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

/** @brief Parsed result returned by one external enrichment API call. */
struct FrameEventsExtProcResult {
    std::string status = "ok";
    std::string result;
    std::string display;
};

class FrameEventsExtProcService {
   public:
    FrameEventsExtProcService(engine::core::messaging::IMessageProducer* producer,
                              engine::pipeline::evidence::FrameEvidenceCache* cache);
    ~FrameEventsExtProcService();

    bool register_handler(const std::string& handler_id, const std::string& pipeline_id,
                          const engine::core::config::FrameEventsExtProcConfig& config);
    bool start();
    void stop();
    bool enqueue(const FrameEventsExtProcJob& job);
    bool is_running() const;

   private:
    struct RegisteredHandler {
        std::string pipeline_id;
        engine::core::config::FrameEventsExtProcConfig config;
    };

    void worker_loop();
    void process_job(void* enc_ctx, const FrameEventsExtProcJob& job);

    engine::core::messaging::IMessageProducer* producer_ = nullptr;
    engine::pipeline::evidence::FrameEvidenceCache* cache_ = nullptr;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<FrameEventsExtProcJob> jobs_;
    std::unordered_map<std::string, RegisteredHandler> handlers_;
    std::unordered_map<std::string, int64_t> throttle_state_ms_;
    std::vector<std::thread> workers_;
    std::atomic<bool> stop_{false};
    bool started_ = false;
    size_t queue_capacity_ = 256;
    size_t worker_count_ = 1;
};

}  // namespace engine::pipeline::extproc