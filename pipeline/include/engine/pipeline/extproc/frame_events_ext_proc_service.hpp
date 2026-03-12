#pragma once

#include "engine/core/config/config_types.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace engine::core::messaging {
class IMessageProducer;
}

typedef struct _NvDsObjectMeta NvDsObjectMeta;
typedef struct _NvDsFrameMeta NvDsFrameMeta;
struct NvBufSurface;

namespace engine::pipeline::extproc {

/** @brief Parsed result returned by one external enrichment API call. */
struct FrameEventsExtProcResult {
    std::string status = "ok";
    std::string result;
    std::string display;
};

/**
 * @brief Live-surface external enrichment sidecar for `trigger: frame_events`.
 *
 * `FrameEventsProbeHandler` publishes the semantic `frame_events` message
 * first, then calls this service for each emitted object. The service resolves
 * the matching rule by `object_type`, encodes the crop while the current batch
 * surface is still mapped, and moves only the detached JPEG bytes into a
 * fire-and-forget HTTP thread.
 */
class FrameEventsExtProcService {
   public:
    explicit FrameEventsExtProcService(engine::core::messaging::IMessageProducer* producer);
    ~FrameEventsExtProcService();

    FrameEventsExtProcService(const FrameEventsExtProcService&) = delete;
    FrameEventsExtProcService& operator=(const FrameEventsExtProcService&) = delete;

    bool register_handler(const std::string& handler_id, const std::string& pipeline_id,
                          const engine::core::config::FrameEventsExtProcConfig& config);
    bool start();
    void stop();
    bool is_running() const;

    /**
     * @brief Applies any cached ext-proc display override to the current frame.
     *
     * HTTP enrichment runs asynchronously, so a result usually arrives after the
     * source frame that triggered it has already passed downstream. The probe
     * calls this method on subsequent frames before `nvdsosd` so the matching
     * tracked object can render the API-provided display text.
     */
    void apply_cached_display_text(const std::string& handler_id, int source_id,
                                   NvDsFrameMeta* frame_meta);

    void process_object(const std::string& handler_id, int source_id,
                        const std::string& source_name, const std::string& frame_key,
                        int64_t frame_ts_ms, const std::string& overview_ref,
                        const std::string& crop_ref, const std::string& object_key,
                        const std::string& instance_key, uint64_t object_id, uint64_t tracker_id,
                        int class_id, const std::string& object_type, double confidence,
                        NvDsObjectMeta* obj_meta, NvDsFrameMeta* frame_meta,
                        NvBufSurface* batch_surf);

   private:
    struct Impl;
    std::shared_ptr<Impl> pimpl_;
};

}  // namespace engine::pipeline::extproc