#pragma once

#include "engine/core/config/config_types.hpp"
#include "engine/core/messaging/imessage_producer.hpp"

#include <gst/gst.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::pipeline::probes {

/**
 * @brief Recording session tracking data.
 *
 * Tracks an active smart-record session started via g_signal_emit_by_name("start-sr")
 * on an nvurisrcbin child element.
 */
struct RecordingSession {
    uint32_t session_id = 0;
    uint32_t source_id = 0;
    std::string source_name;
    GstClockTime start_time = GST_CLOCK_TIME_NONE;  ///< gst_system_clock time at recording start
    uint32_t duration_sec = 0;                      ///< pre + post in seconds
};

/**
 * @brief Per-source recording state.
 *
 * Timing uses `GstClockTime` (nanoseconds from gst_system_clock_obtain()) rather than
 * std::chrono, matching the GStreamer clock domain for accurate interval checks.
 */
struct SourceRecordingState {
    /** @brief GstClockTime of the actual recording start (pre-event adjusted).
     *  GST_CLOCK_TIME_NONE means no recording has been started yet. */
    GstClockTime last_record_time = GST_CLOCK_TIME_NONE;
    std::optional<RecordingSession> active_session;
    gulong signal_handler_id = 0;
};

/**
 * @brief Pad probe handler that triggers NvDsSR smart recording on detection.
 *
 * Attaches to the configured probe element's src pad.  When matching objects
 * are found, locates the per-source nvurisrcbin inside the nvmultiurisrcbin
 * and emits the `start-sr` GSignal to begin recording.
 *
 * ## Key improvements over v1
 * - Uses `GstClockTime` for interval tracking (same clock domain as GStreamer).
 * - `actual_start = current_time - pre_event_ns` so the interval starts from
 *   the true beginning of the recording buffer, not the detection moment.
 * - Stale element cache detection: re-discovers nvurisrcbin if it is no longer
 *   a child of the multiuribin (e.g. after stream hot-swap).
 * - `cleanup_expired_sessions()` clears sessions that never received sr-done
 *   (guards against missed callbacks).
 * - `shutting_down_` atomic flag prevents data races during teardown.
 * - `max_concurrent_recordings` hard cap enforced before emitting start-sr.
 *
 * ## Thread-safety
 * All mutable state is guarded by `mutex_`.  `shutting_down_` is atomic.
 *
 * ## Lifecycle
 * ProbeHandlerManager creates via `new`, calls `configure()`, passes as
 * user_data to `gst_pad_add_probe`.  GDestroyNotify lambda calls `delete`
 * when the probe is removed.
 *
 * ## DS8 integration
 * Requires `libnvdsgst_smartrecord.so` linked and `smart_record > 0` on
 * the nvmultiurisrcbin source element.
 */
class SmartRecordProbeHandler {
   public:
    SmartRecordProbeHandler();
    ~SmartRecordProbeHandler();

    // Non-copyable, non-movable (GStreamer signal connections + mutex)
    SmartRecordProbeHandler(const SmartRecordProbeHandler&) = delete;
    SmartRecordProbeHandler& operator=(const SmartRecordProbeHandler&) = delete;
    SmartRecordProbeHandler(SmartRecordProbeHandler&&) = delete;
    SmartRecordProbeHandler& operator=(SmartRecordProbeHandler&&) = delete;

    /**
     * @brief Configure from full pipeline config and event handler entry.
     *
     * @param config      Full pipeline config (for pipeline_id, cameras).
     * @param handler     The event_handler YAML entry (trigger=smart_record).
     * @param multiuribin Pointer to the nvmultiurisrcbin element (owned by
     *                    pipeline bin — we borrow it).
     * @param producer    Optional message producer for publishing events.
     */
    void configure(const engine::core::config::PipelineConfig& config,
                   const engine::core::config::EventHandlerConfig& handler, GstElement* multiuribin,
                   engine::core::messaging::IMessageProducer* producer);

    /** @brief Static GstPadProbeCallback — user_data is `SmartRecordProbeHandler*`. */
    static GstPadProbeReturn on_buffer(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);

   private:
    /** @brief Process one batch frame-by-frame. */
    GstPadProbeReturn process_batch(GstBuffer* buf, GstClockTime now);

    /** @brief Locate the nvurisrcbin child for a given source_id (cached + stale-check). */
    GstElement* find_nvurisrcbin(uint32_t source_id);

    /**
     * @brief Check min-interval, active-session, and max-concurrent constraints.
     * @param source_id  Source to evaluate.
     * @param now        Current GstClockTime from gst_system_clock_obtain().
     */
    bool can_start_recording(uint32_t source_id, GstClockTime now);

    /**
     * @brief Start a recording via g_signal_emit_by_name("start-sr").
     * @param source_id         Source to record.
     * @param trigger_object_id Tracker object_id that triggered the recording.
     * @param now               Current GstClockTime (used for actual_start calc).
     */
    void start_recording(uint32_t source_id, uint64_t trigger_object_id, GstClockTime now);

    /**
     * @brief Expire sessions that never received sr-done within expected duration + grace.
     * @param now  Current GstClockTime.
     */
    void cleanup_expired_sessions(GstClockTime now);

    /** @brief Count currently active (in-progress) recording sessions. */
    int count_active_recordings() const;

    /** @brief Publish JSON event via IMessageProducer. */
    void publish_event(const std::string& event, uint32_t source_id, const std::string& source_name,
                       uint32_t session_id, uint32_t duration_sec);

    /** @brief Static callback for sr-done signal from nvurisrcbin. */
    static void on_recording_done(GstElement* nvurisrcbin, gpointer recording_info,
                                  gpointer recording_user_data, gpointer user_data);

    /** @brief Get human-readable source name for a source_id. */
    std::string get_source_name(uint32_t source_id) const;

    /** @brief Disconnect all sr-done signal handlers before destruction. */
    void disconnect_all_signals();

    // ── Config ────────────────────────────────────────────────────
    std::string pipeline_id_;
    std::vector<std::string> label_filter_;
    int pre_event_sec_ = 2;
    int post_event_sec_ = 20;
    int min_interval_sec_ = 2;
    int max_concurrent_recordings_ = 0;  ///< 0 = unlimited
    std::string broker_channel_;

    // ── Element references ────────────────────────────────────────
    GstElement* multiuribin_ = nullptr;                      ///< Borrowed, not owned
    std::unordered_map<uint32_t, GstElement*> source_bins_;  ///< Cached nvurisrcbin per source

    // ── State ─────────────────────────────────────────────────────
    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, SourceRecordingState> source_states_;
    std::unordered_map<int, std::string> source_id_to_name_;

    /** @brief Set true in destructor to reject late sr-done callbacks. */
    std::atomic<bool> shutting_down_{false};

    // ── Messaging ─────────────────────────────────────────────────
    engine::core::messaging::IMessageProducer* producer_ = nullptr;
};

}  // namespace engine::pipeline::probes
