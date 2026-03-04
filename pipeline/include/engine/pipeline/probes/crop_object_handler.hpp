#pragma once

/**
 * @file crop_object_handler.hpp
 * @brief Pad probe that crops detected objects from GPU frames as JPEG images.
 *
 * Key features:
 *   - CUDA-accelerated JPEG encoding (NvDsObjEncCtxHandle)
 *   - Structured publish decision: FirstSeen / Bypass / Heartbeat / Exit
 *   - Token bucket bypass for burst captures of newly-detected objects
 *   - Batched message publishing (accumulate -> encode -> finish -> publish)
 *   - Daily directory rotation (YYYYMMDD/) with lantanav2-aligned file naming
 *   - Robust cleanup: stale objects (with Exit metadata generation), old dirs, emergency limit
 *   - PTS-based capture throttling, sanitized file names
 *   - Message ID chain (mid / prev_mid) for event correlation
 *   - ExternalProcessorService integration: HTTP API enrichment per-object rule
 *   - Safe teardown via shutting_down_ atomic flag
 *
 * Publish format aligned with lantanav2 CropObjectHandlerV2:
 *   - Event "crop_bb" with field names: pid, sid, sname, oid, class, conf, etc.
 *   - Flat bbox fields (top, left, w, h) instead of nested object
 *   - Relative file paths (fname, fname_ff) relative to save_dir_base
 *   - Parent fields as empty-string placeholders (for future SGIE support)
 *   - Source frame & pipeline dimensions (s_w_ff, s_h_ff, w_ff, h_ff)
 *   - Timestamp as Unix ms string (event_ts)
 *   - Extra fields retained: class_id, frame_num, tracker_id
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
 * - `Bypass`    -- token bucket burst capture (rapid initial captures).
 * - `Heartbeat` -- periodic re-publish after capture_interval_sec elapsed.
 * - `Exit`      -- object expired from tracking (stale cleanup).
 */
enum class PubDecisionType {
    None,       ///< No publish (throttled, dedup, or filtered)
    FirstSeen,  ///< Object first detected -- immediate publish
    Bypass,     ///< Token bucket burst -- rapid capture of new objects
    Heartbeat,  ///< Periodic heartbeat -- capture_interval_sec elapsed
    Exit        ///< Object exited -- stale cleanup notification
};

/**
 * @brief Per-object publishing state.
 *
 * Tracks last publish timestamp, heartbeat sequence number, last payload
 * hash for dedup, message ID for chain correlation, token bucket state
 * for bypass burst captures, and last SGIE labels for label-change bypass.
 */
struct ObjectPubState {
    GstClockTime last_publish_pts = GST_CLOCK_TIME_NONE;  ///< PTS of last publish
    uint64_t heartbeat_seq = 0;                           ///< Heartbeat sequence counter
    std::size_t last_payload_hash = 0;                    ///< Hash for heartbeat dedup
    std::string last_message_id;                          ///< Last published message ID
    std::string last_instance_key;     ///< Last published instance_key (UUIDv7 per capture)
    uint64_t last_seen_frame_num = 0;  ///< Last observed frame_num for ON-frame hysteresis
    int on_stable_count = 0;           ///< Consecutive observed frames for ON stability
    int bypass_tokens = 0;             ///< Token bucket for burst captures
    GstClockTime last_refill_pts = GST_CLOCK_TIME_NONE;  ///< PTS of last token refill
    std::string last_sgie_labels;     ///< Last published SGIE label string (bypass on change)
    std::string pending_sgie_labels;  ///< Candidate changed signature awaiting stability
    int pending_label_count = 0;      ///< Consecutive frames for pending_sgie_labels
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

    // Pipeline frame dimensions (for s_w_ff/s_h_ff/w_ff/h_ff in publish)
    int pipeline_width_ = 0;   ///< Streammux output width from config
    int pipeline_height_ = 0;  ///< Streammux output height from config

    // Cleanup config
    int stale_object_timeout_min_ = 5;  ///< Remove stale object keys after N minutes
    int check_interval_batches_ = 30;   ///< Cleanup check every N batches
    int old_dirs_max_days_ = 7;         ///< Delete daily dirs older than N days (0=disabled)

    // Publish decision tuning (configurable from event handler YAML)
    int burst_max_ = 3;
    int k_on_frames_ = 5;
    int k_off_frames_ = 2;
    int k_label_frames_ = 5;
    uint64_t token_refill_ns_ = 5'000'000'000ULL;
    uint64_t bypass_min_gap_ns_ = 1'000'000'000ULL;

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

    /**
     * @brief Persistent SGIE child → PGIE parent object_id mapping (keyed by tracker_id).
     *
     * After nvtracker processes the batch, DeepStream does NOT guarantee that
     * obj->parent remains non-null for SGIE child objects (e.g. LP).  This map
     * is populated whenever we observe an explicit obj->parent pointer, and is
     * consulted in subsequent frames when that pointer is null so the child is
     * still grouped with — and references — its correct parent.
     *
     * Entries are erased when the child object becomes stale (same lifecycle as
     * object_last_seen_).
     */
    std::unordered_map<uint64_t, uint64_t>
        child_parent_oid_cache_;  ///< compose_key(src, child_tid) → parent_tid

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

    // -- Memory Stats Log Rate Limiter ---------------------------------------
    int64_t last_memory_stats_log_epoch_ms_ = 0;     ///< Epoch ms of last memory-stats log
    int64_t memory_stats_log_interval_ms_ = 60'000;  ///< Minimum gap between memory-stats logs

    // -- Pending Message (batch accumulation) --------------------------------

    /**
     * @brief Data for one pending detection message.
     *
     * Field names aligned with lantanav2 CropObjectHandlerV2 publish format.
     * fname/fname_ff are paths relative to save_dir_base (not absolute).
     */
    struct PendingMessage {
        int source_id;
        std::string source_name;
        std::string object_key;
        std::string instance_key;
        std::string
            parent_object_key;  ///< Parent's persistent UUID (empty if no parent / PGIE obj)
        std::string parent_instance_key;  ///< Parent's per-batch instance UUID (empty if no parent)
        std::string parent_id_str;  ///< Parent's tracker_id as decimal string (empty if no parent)
        int class_id;
        std::string label;   ///< PGIE class name (obj_label)
        std::string labels;  ///< SGIE classifier results: "label:id:prob|..." (empty if no SGIE)
        float confidence;
        uint64_t tracker_id;
        float left, top, width, height;  ///< Bounding box (published as flat top/left/w/h)
        std::string fname;               ///< Relative crop JPEG path (from save_dir_base)
        std::string fname_ff;            ///< Relative full-frame JPEG path
        int64_t timestamp_ms;
        uint64_t frame_num;
        std::string message_id;       ///< Unique ID for this message
        std::string prev_message_id;  ///< Previous message ID for this object (chain)
        PubDecisionType pub_type;     ///< Why this message is being published
        std::string pub_reason;       ///< Human-readable reason string
        uint64_t heartbeat_seq;       ///< Heartbeat sequence (0 for FirstSeen)
        int source_frame_width = 0;   ///< NvBufSurface frame width
        int source_frame_height = 0;  ///< NvBufSurface frame height
        int pipeline_width = 0;       ///< Streammux output width
        int pipeline_height = 0;      ///< Streammux output height
    };

    /**
     * @brief Parent-child object group built from one frame's object metadata.
     */
    struct ObjectGroup {
        NvDsObjectMeta* parent = nullptr;
        uint64_t parent_tid = UNTRACKED_OBJECT_ID;
        std::vector<NvDsObjectMeta*> children;
    };

    /**
     * @brief Immutable per-frame context used by helper processing methods.
     */
    struct FrameProcessContext {
        NvDsFrameMeta* frame_meta = nullptr;
        int source_id = 0;
        GstClockTime frame_pts = GST_CLOCK_TIME_NONE;
        int64_t timestamp_ms = 0;
        std::string source_name;
        std::string realtime_str;
        int source_frame_width = 0;
        int source_frame_height = 0;
    };

    // -- Internal Methods ----------------------------------------------------

    /** @brief Main processing logic for one batch buffer. */
    GstPadProbeReturn process_batch(GstBuffer* buf);

    /** @brief Process one frame from batch metadata and append pending messages. */
    void process_frame(const FrameProcessContext& frame_ctx, NvBufSurface* ip_surf,
                       std::vector<PendingMessage>& pending_messages);

    /** @brief Build parent-child object groups for a frame in an order-independent way. */
    void build_object_groups(const FrameProcessContext& frame_ctx,
                             std::vector<ObjectGroup>& groups);

    /** @brief Build stable SGIE decision signature for a parent group. */
    std::string build_group_sgie_signature(const ObjectGroup& group) const;

    /** @brief Preload parent instance keys from previous publish state for this frame. */
    std::unordered_map<uint64_t, std::string> build_parent_instance_key_cache(
        const FrameProcessContext& frame_ctx, const std::vector<ObjectGroup>& groups) const;

    /** @brief Resolve source name by source id with fallback naming. */
    std::string resolve_source_name(int source_id) const;

    /**
     * @brief Encode and accumulate one object publish candidate.
     * @return true if the object produced a pending message.
     */
    bool process_object_for_publish(
        NvDsObjectMeta* obj, const FrameProcessContext& frame_ctx, bool is_parent_role,
        uint64_t parent_tid_hint, bool has_forced_decision, PubDecisionType forced_decision,
        const std::string* sgie_labels_override, const std::string* decision_signature_override,
        bool skip_heartbeat_dedup,
        std::unordered_map<uint64_t, std::string>& frame_parent_inst_keys,
        std::vector<PendingMessage>& pending_messages, NvBufSurface* ip_surf);

    /** @brief Ensure daily directory exists (rotates on date change). */
    bool ensure_daily_dir();

    /** @brief Delete daily directories older than old_dirs_max_days_. Rate-limited to 1/hour. */
    void cleanup_old_directories();

    /**
     * @brief Determine whether this object should be captured and what publish type.
     *
     * Decision logic (in order):
     *   1. ON hysteresis — object must appear for K_ON_FRAMES consecutive frames,
     *      allowing at most K_OFF_FRAMES frame distance between observations.
     *   2. FirstSeen — first publish after ON hysteresis is satisfied.
     *   3. Heartbeat — capture interval elapsed.
     *   4. Bypass — within capture interval, changed signature is stable for
     *      K_LABEL_FRAMES consecutive frames, token available, and debounce gap passed.
     *   5. None — otherwise.
     *
     * Bypass is modelled after lantanav2 LabelStateV2: fires only when SGIE labels
     * change between publishes, consuming one burst token. This avoids spamming
     * burst frames and instead captures meaningful state transitions.
     *
     * @param key          compose_key(source_id, tracker_id)
     * @param current_pts  Current buffer PTS.
     * @param sgie_labels  Current SGIE label string (empty if no SGIE running).
     * @param frame_num    Current frame number for ON hysteresis tracking.
     * @return PubDecisionType::FirstSeen, Heartbeat, Bypass, or None.
     */
    PubDecisionType decide_capture(uint64_t key, GstClockTime current_pts,
                                   const std::string& sgie_labels, uint64_t frame_num);

    /**
     * @brief Compute a payload hash for dedup (class_id + bbox + label + sgie_labels).
     *
     * If hash matches the last published heartbeat for this object, the
     * heartbeat is suppressed to avoid redundant messages.
     * Including sgie_labels ensures a label change flushes the dedup cache.
     */
    static std::size_t compute_payload_hash(int class_id, const std::string& label,
                                            const std::string& sgie_labels, float left, float top,
                                            float width, float height);

    /**
     * @brief Extract SGIE classifier labels from NvDsClassifierMeta.
     *
     * Format: "result_label:label_id:result_prob|..." (pipe-separated per label).
     * Matches lantanav2 build_labels_string_verbose() output.
     * Returns empty string if no classifier metadata present.
     *
     * @param obj NvDsObjectMeta from which to extract classifier results.
     * @return Formatted SGIE labels string.
     */
    static std::string build_sgie_labels(NvDsObjectMeta* obj);

    /** @brief Get or create persistent object_key for a tracker ID. */
    std::string get_or_create_object_key(int source_id, uint64_t tracker_id);

    /** @brief Mark tracker object as seen at given PTS. */
    void update_object_last_seen(int source_id, uint64_t tracker_id, GstClockTime pts);

    /**
     * @brief Remove stale object entries not seen within timeout.
     * @param current_pts Current PTS for staleness comparison.
     * @param timestamp_ms Current epoch ms for Exit message timestamps.
     * @return Vector of Exit PendingMessages for removed objects.
     */
    std::vector<PendingMessage> cleanup_stale_objects(GstClockTime current_pts,
                                                      int64_t timestamp_ms);

    /** @brief Log comprehensive memory stats for all state maps. */
    void log_memory_stats() const;

    /** @brief Return true when memory stats log is allowed by time-based rate limit. */
    bool should_log_memory_stats(int64_t now_epoch_ms);

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

    /** @brief Convert PubDecisionType to a human-readable reason string. */
    static const char* pub_reason_for_type(PubDecisionType type);

    /**
     * @brief Generate wall-clock real-time string for file naming.
     *
     * Format: YYYYMMDD_HHMMSS_mmm (e.g. "20250703_143052_123").
     * Matches lantanav2 get_formatted_real_time() format.
     */
    static std::string generate_realtime_str();

    /**
     * @brief Compute relative path from save_dir_base.
     * @param absolute_path Full absolute file path.
     * @return Path relative to save_dir_base_ (e.g. "20250703/filename.jpg").
     */
    std::string relative_path(const std::string& absolute_path) const;

    // -- Token Bucket (Bypass) -----------------------------------------------

    /**
     * @brief Refill bypass tokens based on elapsed PTS time.
     * @param state  Per-object publish state (modified in place).
     * @param now_pts Current PTS timestamp.
     */
    void refill_tokens(ObjectPubState& state, GstClockTime now_pts);

    /**
     * @brief Attempt to consume one bypass token.
     * @param state  Per-object publish state (modified in place).
     * @return true if a token was consumed (bypass allowed).
     */
    bool attempt_bypass(ObjectPubState& state);
};

}  // namespace engine::pipeline::probes
