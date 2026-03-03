// pipeline/include/engine/pipeline/probes/ext_proc_svc.hpp
#pragma once

#include "engine/core/config/config_types.hpp"
#include "engine/core/messaging/imessage_producer.hpp"

#include <memory>
#include <string>

// Forward-declared DeepStream / GStreamer types to keep the header light
typedef struct _NvDsObjectMeta NvDsObjectMeta;
typedef struct _NvDsFrameMeta NvDsFrameMeta;
struct NvBufSurface;

namespace engine::pipeline::probes {

/**
 * @brief External processor service — HTTP-based AI enrichment for detected
 *        objects (e.g., face recognition, license-plate lookup).
 *
 * For each detected object whose label matches a configured rule, the service:
 *  1. Encodes the object crop as an in-memory JPEG (NvDsObjEnc, saveImg=FALSE).
 *  2. HTTP-POSTs the JPEG as multipart/form-data to the rule's endpoint.
 *  3. Parses a JSON response field via a dot-notation path (e.g. "match.id").
 *  4. Publishes the enrichment result to a Redis/Kafka stream channel.
 *
 * API calls are non-blocking — each call runs in a short-lived detached thread
 * so the GStreamer pad probe returns immediately.
 *
 * A per-(source, tracker-id, label) throttle prevents spamming the endpoint for
 * the same moving object.
 *
 * Designed to be owned by CropObjectHandler and called inside the pad-probe
 * callback WHILE the NvBufSurface is still mapped (before nvds_obj_enc_finish
 * and NvBufSurfaceUnmap are called on the file-saving encoder).
 *
 * Reference: lantanav2 ExternalProcessingServiceV2
 * (backends/deepstream/src/services/ext_proc_svc_v2.cpp)
 */
class ExternalProcessorService {
   public:
    ExternalProcessorService();
    ~ExternalProcessorService();

    // Non-copyable, non-movable (owns NvDsObjEncCtxHandle)
    ExternalProcessorService(const ExternalProcessorService&) = delete;
    ExternalProcessorService& operator=(const ExternalProcessorService&) = delete;

    /**
     * @brief Configures the service.  Must be called once before process_object.
     *
     * @param config      Parsed ext_processor config block (rules, interval).
     * @param pipeline_id Pipeline ID string — embedded in every published event.
     * @param producer    Borrowed IMessageProducer pointer; may be nullptr (disables publishing).
     * @param channel     Redis/Kafka channel name to publish results to.
     */
    void configure(const engine::core::config::ExtProcessorConfig& config,
                   const std::string& pipeline_id,
                   engine::core::messaging::IMessageProducer* producer, const std::string& channel);

    /**
     * @brief Processes one detected object: checks label rule, throttle, encodes
     *        in-memory JPEG, then launches a non-blocking HTTPS API call.
     *
     * MUST be called while @p batch_surf is still GPU-mapped (i.e., inside the
     * pad-probe batch loop, before the caller calls NvBufSurfaceUnmap).
     *
     * Expensive operations (CURL request, JSON parsing, Redis publish) run in a
     * detached thread — this method returns quickly.
     *
     * @param obj_meta     DeepStream object metadata (label, bbox, tracker id …).
     * @param frame_meta   DeepStream frame metadata (source_id, pad_index …).
     * @param batch_surf   Currently-mapped NvBufSurface for the batch buffer.
     * @param source_name  Human-readable camera / source name.
     * @param instance_key Persistent instance key string (from CropObjectHandler).
     * @param object_key   Persistent per-object key string (from CropObjectHandler).
     */
    void process_object(NvDsObjectMeta* obj_meta, NvDsFrameMeta* frame_meta,
                        NvBufSurface* batch_surf, const std::string& source_name,
                        const std::string& instance_key, const std::string& object_key);

   private:
    struct Impl;
    // Shared ptr — keeps Impl alive inside detached threads that captured it
    std::shared_ptr<Impl> pimpl_;
};

}  // namespace engine::pipeline::probes
