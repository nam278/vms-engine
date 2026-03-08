#pragma once

#include "engine/core/config/config_types.hpp"

#include <cstdint>
#include <gst/gst.h>
#include <gstnvdsmeta.h>
#include <nvbufsurface.h>
#include <nvds_obj_encode.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace engine::core::messaging {
class IMessageProducer;
}

namespace engine::pipeline::evidence {

class FrameEvidenceCache;
struct CachedFrameEntry;
struct FrameObjectSnapshot;

/** @brief One requested object crop; can match by key or fall back to a caller-supplied bbox. */
struct EvidenceRequestObject {
    std::string object_key;
    std::string instance_key;
    std::string crop_ref;
    int64_t object_id = -1;
    bool has_bbox = false;
    float left = 0.0F;
    float top = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

/** @brief Parsed evidence request queued for asynchronous materialization. */
struct EvidenceRequestJob {
    std::string schema_version;
    std::string request_id;
    std::string pipeline_id;
    std::string source_name;
    int source_id = 0;
    std::string frame_key;
    int64_t frame_ts_ms = 0;
    std::string overview_ref;
    std::string event_id;
    std::string timeline_id;
    std::vector<std::string> evidence_types;
    std::vector<EvidenceRequestObject> objects;
    std::string raw_payload;
};

class EvidenceRequestService {
   public:
    EvidenceRequestService(const engine::core::config::EvidenceConfig& config,
                           engine::core::messaging::IMessageProducer* producer,
                           FrameEvidenceCache* cache);
    ~EvidenceRequestService();

    bool start();
    void stop();
    bool enqueue_request(const std::string& payload);

   private:
    // Parse and validate the broker payload before it enters the worker queue.
    bool parse_request_payload(const std::string& payload, EvidenceRequestJob& out_job) const;
    // Owns NvDsObjEnc context and performs encode work off the pipeline thread.
    void worker_loop();
    // Resolve the cached frame, encode requested artifacts, then publish completion.
    void process_job(const EvidenceRequestJob& job);
    bool encode_overview(const CachedFrameEntry& entry, const EvidenceRequestJob& job,
                         std::string& out_ref, std::string& failure_reason);
    bool encode_crops(const CachedFrameEntry& entry, const EvidenceRequestJob& job,
                      std::vector<std::string>& out_refs, std::string& failure_reason);
    bool resolve_output_path(const std::string& ref, std::string& out_path,
                             std::string& failure_reason) const;
    void publish_ready(const EvidenceRequestJob& job, const std::string& status,
                       const std::string& overview_ref, const std::vector<std::string>& crop_refs,
                       const std::string& failure_reason) const;

    engine::core::config::EvidenceConfig config_;
    engine::core::messaging::IMessageProducer* producer_ = nullptr;
    FrameEvidenceCache* cache_ = nullptr;
    std::atomic<bool> stop_{false};
    std::thread worker_thread_;
    std::condition_variable cv_;
    mutable std::mutex queue_mutex_;
    std::queue<EvidenceRequestJob> jobs_;
    NvDsObjEncCtxHandle enc_ctx_ = nullptr;
};

}  // namespace engine::pipeline::evidence