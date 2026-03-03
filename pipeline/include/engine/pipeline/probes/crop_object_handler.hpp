#pragma once

/**
 * @file crop_object_handler.hpp
 * @brief Pad probe that crops detected objects from GPU frames as JPEG images.
 *
 * Key features:
 *   - CUDA-accelerated JPEG encoding (NvDsObjEncCtxHandle)
 *   - Structured publish decision: FirstSeen / Heartbeat with payload hash dedup
 *   - Batched message publishing (accumulate -> encode -> finish -> publish)
 *   - Daily directory rotation (YYYYMMDD/src_N/)
 *   - Robust cleanup: stale objects, old dirs, emergency hard limit
 *   - PTS-based capture throttling, sanitized file names
 *   - Message ID chain (mid / prev_mid) for event correlation
 *   - ExternalProcessorService integration: HTTP API enrichment per-object rule
 *   - Safe teardown via shutting_down_ atomic flag
 *
 * Improvements over V1 (ported from lantanav2 CropObjectHandlerV2):
 *   - PubDecisionType enum for publish classification
 *   - Payload hash dedup suppresses duplicate heartbeat messages
 *   - Batch-accumulate-then-publish pattern reduces mutex hold time
 *   - Comprehensive stale cleanup across ALL state maps
 *   - Detailed memory stats logging at cleanup intervals
 *   - Message ID tracking for event chain correlation
 *   - Sanitized labels in file names (invalid chars replaced)
 *   - ext_processor config struct (implementation is a future extension)
 */

#include "engine/core/config/config_types.hpp"
#include "engine/core/messaging/imessage_producer.hpp"
#include "engine/core/utils/uuid_v7_generator.hpp"
#include "engine/pipeline/probes/ext_proc_svc.hpp"

#include <gst/gst.h>
#include <nvbufsurface.h>
#include <gstnvdsmeta.h>
#include <nvds_obj_encode.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine::pipeline::probes {

// ============================================================================
// Publish Decision Types
// ============================================================================

/**
 * @brief Classification of why a detection message is being published.
 *
 * - `None`      -- no publish this cycle (throttled or dedup suppressed).
 * - `FirstSeen` -- object first detected (new tracker ID).
 * - `Heartbeat` -- periodic re-publish after capture_interval_sec elapsed.
 */
enum class PubDecisionType {
    None,       ///< No publish (throttled, dedup, or filtered)
    FirstSeen,  ///< Object first detected -- immediate publish
    Heartbeat   ///< Periodic heartbeat -- capture_interval_sec elapsed
};

/**
 * @brief Per-object publishing state.
 *
 * Tracks last publish timestamp, heartbeat sequence number, last payload
 * hash for dedup, and message ID for chain correlation.
 */
struct ObjectPubState {
    GstClockTime last_publish_pts = GST_CLOCK_TIME_NONE;  ///< PTS of last publish
    uint64_t heartbeat_seq = 0;                           ///< Heartbeat sequence counter
    std::size_t last_payload_hash = 0;                    ///< Hash for heartbeat dedup
    std::string last_message_id;                          ///< Last published message ID
};

// ============================================================================
// CropObjectHandler
// ============================================================================

/**
 * @brief Pad probe that crops detected objects from GPU frames as JPEG images.
 *
 * For each batch buffer, maps NvBufSurface, iterates over NvDs object metadata,
 * encodes crops (and optionally full-frame snapshots) via the DeepStream
 * NvDsObjEnc CUDA-accelerated JPEG encoder, then publishes detection metadata
 * as JSON to a message broker (Redis Streams).
 *
 * Processing flow per batch:
 *   1. Map GstBuffer -> NvBufSurface, cudaDeviceSynchronize()
 *   2. Ensure daily output directory exists
 *   3. Iterate frames -> objects -> apply label filter
 *   4. For each qualifying object:
 *      a. Determine PubDecisionType (FirstSeen / Heartbeat / None)
 *      b. Compute payload hash; skip if Heartbeat and hash unchanged (dedup)
 *      c. Encode crop JPEG + optional full-frame JPEG
 *      d. Accumulate pending message
 *   5. nvds_obj_enc_finish() -- release all accumulated CUDA surfaces
 *   6. Publish all pending messages to broker
 *   7. Periodic cleanup (stale objects, old dirs, memory stats)
 *
 * Ownership: Created by ProbeHandlerManager, lifetime managed via
 * GDestroyNotify. Destructor handles encoder context destruction.
 *
 * @note nvds_obj_enc_finish() is called every batch (even if no encodes
 *       happened) to release accumulated CUDA surfaces.
 */
class CropObjectHandler {
   public:
    CropObjectHandler();
    ~CropObjectHandler();

    // Non-copyable, non-movable (owns encoder context)
    CropObjectHandler(const CropObjectHandler&) = delete;
    CropObjectHandler& operator=(const CropObjectHandler&) = delete;

    /**
     * @brief Configure from pipeline config and event handler entry.
     *
     * Extracts crop-specific parameters, builds source_id to camera name lookup,
     * creates NvDsObjEnc context, and captures pipeline start time for
     * PTS to absolute-time conversion.
     *
     * @param config    Full pipeline configuration.
     * @param handler   The specific event handler config for this probe.
     * @param producer  Optional message producer for publishing (nullable).
     */
    void configure(const engine::core::config::PipelineConfig& config,
                   const engine::core::config::EventHandlerConfig& handler,
                   engine::core::messaging::IMessageProducer* producer = nullptr);

    /** @brief Static probe callback. user_data -> CropObjectHandler*. */
    static GstPadProbeReturn on_buffer(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);

   private:
    // -- Configuration -------------------------------------------------------
    std::string pipeline_id_;
    std::vector<std::string> label_filter_;
    std::string save_dir_base_;         ///< Base directory for all crops
    uint64_t capture_interval_ns_ = 0;  ///< Min ns between crops per object (PTS-based)
    int image_quality_ = 85;            ///< JPEG quality 1-100
    bool save_full_frame_ = true;       ///< Capture full-frame alongside crops

    // Cleanup config
    int stale_object_timeout_min_ = 5;  ///< Remove stale object keys after N minutes
    int check_interval_batches_ = 30;   ///< Cleanup check every N batches
    int old_dirs_max_days_ = 7;         ///< Delete daily dirs older than N days (0=disabled)

    // Camera name lookup: source_id -> camera name
    std::unordered_map<int, std::string> source_id_to_name_;

    // -- Encoder Context -----------------------------------------------------
    NvDsObjEncCtxHandle enc_ctx_ = nullptr;

    // -- External Processor Service ------------------------------------------
    /** @brief Optional HTTP-based enrichment service (face-rec, plate lookup, …). */
    std::unique_ptr<ExternalProcessorService> ext_proc_svc_;

    // -- Message Broker ------------------------------------------------------
    engine::core::messaging::IMessageProducer* producer_ = nullptr;  ///< Borrowed, not owned
    std::string broker_channel_;

    // -- UUID Generator ------------------------------------------------------
    engine::core::utils::UuidV7Generator uuid_gen_;

    // -- Pipeline Start Time -------------------------------------------------
    /** @brief System-clock nanoseconds at pipeline start, for PTS to absolute conversion. */
    uint64_t pipeline_start_time_ns_ = 0;

    // -- Per-Object State (keyed by compose_key(source_id, tracker_id)) ------
    std::unordered_map<uint64_t, std::string> object_keys_;  ///< Persistent UUIDv7 per tracker ID
    std::unordered_map<uint64_t, GstClockTime> object_last_seen_;  ///< Last PTS per tracker ID
    std::unordered_map<uint64_t, GstClockTime> last_capture_pts_;  ///< Last capture PTS per object
    std::unordered_map<uint64_t, ObjectPubState> pub_state_;       ///< Publish state per object

    // -- Batch State (reset per batch) ---------------------------------------
    /** @brief Full-frame paths already captured this batch (keyed by frame_num). */
    std::unordered_map<uint64_t, std::string> full_frame_paths_this_batch_;

    // -- Counters & Synchronization ------------------------------------------
    int batch_counter_ = 0;
    std::mutex mutex_;
    std::atomic<bool> shutting_down_{false};  ///< Set in destructor, checked in on_buffer

    // -- Daily Directory -----------------------------------------------------
    int current_date_yyyymmdd_ = 0;  ///< Current date as integer YYYYMMDD
    std::string current_day_dir_;    ///< Full path to current day directory

    // -- Old Directory Cleanup Rate Limiter ----------------------------------
    int64_t last_old_dir_cleanup_epoch_ = 0;  ///< Epoch seconds of last old-dir cleanup

    // -- Pending Message (batch accumulation) --------------------------------

    /** @brief Data for one pending detection message. */
    struct PendingMessage {
        int source_id;
        std::string source_name;
        std::string object_key;
        std::string instance_key;
        int class_id;
        std::string label;
        float confidence;
        uint64_t tracker_id;
        float left, top, width, height;
        std::string crop_path;
        std::string full_frame_path;
        int64_t timestamp_ms;
        uint64_t frame_num;
        std::string message_id;       ///< Unique ID for this message
        std::string prev_message_id;  ///< Previous message ID for this object (chain)
        PubDecisionType pub_type;     ///< Why this message is being published
        uint64_t heartbeat_seq;       ///< Heartbeat sequence (0 for FirstSeen)
    };

    // -- Internal Methods ----------------------------------------------------

    /** @brief Main processing logic for one batch buffer. */
    GstPadProbeReturn process_batch(GstBuffer* buf);

    /** @brief Ensure daily directory exists (rotates on date change). */
    bool ensure_daily_dir();

    /** @brief Build source sub-directory path (creates if needed). */
    std::string source_dir(int source_id) const;

    /** @brief Delete daily directories older than old_dirs_max_days_. Rate-limited to 1/hour. */
    void cleanup_old_directories();

    /**
     * @brief Determine whether this object should be captured and what publish type.
     *
     * @param key        compose_key(source_id, tracker_id)
     * @param current_pts Current buffer PTS.
     * @return PubDecisionType::FirstSeen, Heartbeat, or None.
     */
    PubDecisionType decide_capture(uint64_t key, GstClockTime current_pts) const;

    /**
     * @brief Compute a payload hash for dedup (class_id + bbox + label).
     *
     * If hash matches the last published heartbeat for this object, the
     * heartbeat is suppressed to avoid redundant messages.
     */
    static std::size_t compute_payload_hash(int class_id, const std::string& label, float left,
                                            float top, float width, float height);

    /** @brief Get or create persistent object_key for a tracker ID. */
    std::string get_or_create_object_key(int source_id, uint64_t tracker_id);

    /** @brief Mark tracker object as seen at given PTS. */
    void update_object_last_seen(int source_id, uint64_t tracker_id, GstClockTime pts);

    /** @brief Remove stale object entries not seen within timeout. */
    void cleanup_stale_objects(GstClockTime current_pts);

    /** @brief Log comprehensive memory stats for all state maps. */
    void log_memory_stats() const;

    /** @brief Compose key for (source_id, tracker_id) maps. */
    static uint64_t compose_key(int source_id, uint64_t tracker_id);

    /** @brief Generate a message ID for chain correlation. */
    static std::string generate_message_id(int source_id, uint64_t frame_num, int64_t timestamp_ms);

    /**
     * @brief Publish all pending messages to broker.
     *
     * Called after nvds_obj_enc_finish() to ensure crop files are written before
     * consumers receive the message. Uses the batch-accumulate pattern from V2.
     */
    void publish_pending_messages(const std::vector<PendingMessage>& messages);

    /**
     * @brief Sanitize a label string for safe use in file names.
     *
     * Replaces non-alphanumeric chars (except '-' and '_') with '_',
     * truncates to max 30 chars.
     */
    static std::string sanitize_label(const std::string& label);

    /** @brief Get current date as YYYYMMDD integer. */
    static int today_yyyymmdd();

    /** @brief Format YYYYMMDD integer into directory name string. */
    static std::string format_date_dir(int yyyymmdd);

    /** @brief Get current time as epoch milliseconds (wall-clock). */
    static int64_t now_epoch_ms();

    /** @brief Convert PubDecisionType to string for logging/JSON. */
    static const char* pub_type_name(PubDecisionType type);
};

}  // namespace engine::pipeline::probes
