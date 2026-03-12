#include "engine/pipeline/probes/smart_record_probe_handler.hpp"
#include "engine/pipeline/source_identity_registry.hpp"
#include "engine/pipeline/source_naming.hpp"
#include "engine/core/utils/logger.hpp"

#include <gst-nvdssr.h>
#include <gstnvdsmeta.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>

using json = nlohmann::json;

namespace engine::pipeline::probes {

namespace {

/// GObject qdata key: attach source_id to nvurisrcbin for reliable callback mapping
constexpr const char* kSourceIdDataKey = "engine_source_id";

/// Grace period added to expected recording duration when expiring stale sessions
constexpr GstClockTime kSessionGracePeriodNs = 5 * GST_SECOND;

/**
 * @brief Obtain the current GstSystemClock time (nanoseconds).
 *
 * The GstSystemClock singleton is ref-counted; we must unref after use.
 */
static inline GstClockTime get_gst_clock_now() {
    GstClock* clock = gst_system_clock_obtain();
    GstClockTime now = gst_clock_get_time(clock);
    gst_object_unref(clock);
    return now;
}

/**
 * @brief Find nvurisrcbin element by source_id inside the nvmultiurisrcbin.
 *
 * DS8 naming convention: nvmultiurisrcbin contains a creator bin which holds
 * nvurisrcbin elements named "dsnvurisrcbin{source_id}".
 *
 * @return Newly-referenced GstElement*, or nullptr.  Caller must unref.
 */
static GstElement* find_nvurisrcbin_in_bin(GstElement* multiuribin, uint32_t source_id) {
    if (!multiuribin || !GST_IS_BIN(multiuribin))
        return nullptr;

    // Try: {multiuribin_name}_creator sub-bin
    const gchar* parent_name = GST_ELEMENT_NAME(multiuribin);
    gchar* creator_name = g_strdup_printf("%s_creator", parent_name);
    GstElement* creator_bin = gst_bin_get_by_name(GST_BIN(multiuribin), creator_name);
    g_free(creator_name);

    if (!creator_bin || !GST_IS_BIN(creator_bin)) {
        if (creator_bin)
            gst_object_unref(creator_bin);
        return nullptr;
    }

    // Find "dsnvurisrcbin{source_id}" inside the creator bin
    gchar* target_name = g_strdup_printf("dsnvurisrcbin%u", source_id);
    GstElement* urisrcbin = gst_bin_get_by_name(GST_BIN(creator_bin), target_name);
    g_free(target_name);
    gst_object_unref(creator_bin);

    // Validate factory type
    if (urisrcbin) {
        GstElementFactory* factory = gst_element_get_factory(urisrcbin);
        if (factory) {
            const gchar* fname = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
            if (!g_str_has_prefix(fname, "nvurisrcbin")) {
                LOG_W("SmartRecord: element for source {} is not nvurisrcbin (type: {})", source_id,
                      fname);
                gst_object_unref(urisrcbin);
                return nullptr;
            }
        }
    }

    return urisrcbin;  // Caller owns the reference (gst_bin_get_by_name adds a ref)
}

static GstElement* find_nvurisrcbin_in_manual_bin(GstElement* source_root,
                                                  const std::string& camera_id) {
    if (!source_root || !GST_IS_BIN(source_root) || camera_id.empty()) {
        return nullptr;
    }

    return gst_bin_get_by_name(GST_BIN(source_root),
                               engine::pipeline::make_source_element_name(camera_id).c_str());
}

/**
 * @brief Wall-clock milliseconds since Unix epoch (for JSON timestamps).
 */
static inline int64_t now_epoch_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

// ── Constructor / Destructor ───────────────────────────────────────

SmartRecordProbeHandler::SmartRecordProbeHandler() = default;

SmartRecordProbeHandler::~SmartRecordProbeHandler() {
    // Signal teardown flag first so on_recording_done callbacks see it
    shutting_down_.store(true, std::memory_order_seq_cst);

    disconnect_all_signals();

    // Release cached nvurisrcbin refs (we own them from gst_bin_get_by_name)
    for (auto& [sid, elem] : source_bins_) {
        if (elem) {
            gst_object_unref(elem);
        }
    }
    source_bins_.clear();
}

// ── Configure ──────────────────────────────────────────────────────

void SmartRecordProbeHandler::configure(const engine::core::config::PipelineConfig& config,
                                        const engine::core::config::EventHandlerConfig& handler,
                                        GstElement* source_root,
                                        engine::core::messaging::IMessageProducer* producer) {
    pipeline_id_ = config.pipeline.id;
    label_filter_ = handler.label_filter;
    pre_event_sec_ = handler.pre_event_sec;
    post_event_sec_ = handler.post_event_sec;
    min_interval_sec_ = handler.min_interval_sec;
    max_concurrent_recordings_ = handler.max_concurrent_recordings;
    source_root_ = source_root;
    source_type_ = config.sources.type;
    producer_ = producer;
    broker_channel_ = handler.channel;  // empty = no publish

    // Build source_id → camera-name lookup from sources.cameras[]
    for (int i = 0; i < static_cast<int>(config.sources.cameras.size()); ++i) {
        source_id_to_name_[i] = config.sources.cameras[i].id;
    }

    LOG_I(
        "SmartRecord: configured — labels={}, pre={}s, post={}s, "
        "interval={}s, max_concurrent={}, source_root={}, type='{}', broker='{}'",
        label_filter_.size(), pre_event_sec_, post_event_sec_, min_interval_sec_,
        max_concurrent_recordings_, source_root_ ? GST_ELEMENT_NAME(source_root_) : "null",
        source_type_, broker_channel_);
}

// ── Static Probe Callback ──────────────────────────────────────────

GstPadProbeReturn SmartRecordProbeHandler::on_buffer(GstPad* /*pad*/, GstPadProbeInfo* info,
                                                     gpointer user_data) {
    auto* self = static_cast<SmartRecordProbeHandler*>(user_data);
    if (self->shutting_down_.load(std::memory_order_relaxed))
        return GST_PAD_PROBE_OK;

    GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!buf)
        return GST_PAD_PROBE_OK;

    // Get current GstSystemClock time once per batch (avoids repeated syscalls)
    GstClockTime now = get_gst_clock_now();
    return self->process_batch(buf, now);
}

// ── Main Batch Processing ──────────────────────────────────────────

GstPadProbeReturn SmartRecordProbeHandler::process_batch(GstBuffer* buf, GstClockTime now) {
    NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buf);
    if (!batch_meta)
        return GST_PAD_PROBE_OK;

    // Periodically expire sessions that never received sr-done
    cleanup_expired_sessions(now);

    for (NvDsMetaList* l_frame = batch_meta->frame_meta_list; l_frame; l_frame = l_frame->next) {
        auto* frame_meta = static_cast<NvDsFrameMeta*>(l_frame->data);
        if (!frame_meta)
            continue;

        const uint32_t source_id = frame_meta->source_id;

        for (NvDsMetaList* l_obj = frame_meta->obj_meta_list; l_obj; l_obj = l_obj->next) {
            auto* obj = static_cast<NvDsObjectMeta*>(l_obj->data);
            if (!obj)
                continue;

            // Label filter — empty filter accepts everything
            if (!label_filter_.empty()) {
                const std::string label(obj->obj_label);
                auto it = std::find(label_filter_.begin(), label_filter_.end(), label);
                if (it == label_filter_.end())
                    continue;
            }

            // Try to start recording for this source
            if (can_start_recording(source_id, now)) {
                start_recording(source_id, obj->object_id, now);
            }

            // One trigger per source per batch is enough
            break;
        }
    }

    return GST_PAD_PROBE_OK;
}

// ── nvurisrcbin Lookup (with stale-cache detection) ────────────────

GstElement* SmartRecordProbeHandler::find_nvurisrcbin(uint32_t source_id) {
    // Check cache with stale validation
    auto it = source_bins_.find(source_id);
    if (it != source_bins_.end() && it->second) {
        GstElement* cached = it->second;

        if (source_root_ && GST_IS_BIN(source_root_)) {
            GstElement* probe =
                gst_bin_get_by_name(GST_BIN(source_root_), GST_ELEMENT_NAME(cached));
            if (probe) {
                const bool still_same = (probe == cached);
                gst_object_unref(probe);  // extra ref from gst_bin_get_by_name
                if (still_same)
                    return cached;
            }
        } else {
            return cached;  // No bin to validate against — trust cache
        }

        // Cached element is no longer in the pipeline → evict
        LOG_W("SmartRecord: cached nvurisrcbin for source {} is stale, re-discovering", source_id);
        gst_object_unref(cached);  // Release our ref
        source_bins_.erase(it);
    }

    if (!source_root_)
        return nullptr;

    GstElement* urisrcbin = nullptr;
    if (source_type_ == "nvmultiurisrcbin") {
        urisrcbin = find_nvurisrcbin_in_bin(source_root_, source_id);
    } else {
        urisrcbin = find_nvurisrcbin_in_manual_bin(source_root_, get_source_name(source_id));
    }

    if (!urisrcbin) {
        LOG_W("SmartRecord: nvurisrcbin not found for source {}", source_id);
        return nullptr;
    }

    // Cache it (owns the ref from find_nvurisrcbin_in_bin)
    source_bins_[source_id] = urisrcbin;

    // Attach source_id as GObject qdata for reliable callback-to-source mapping
    g_object_set_data(G_OBJECT(urisrcbin), kSourceIdDataKey, GUINT_TO_POINTER(source_id));

    // Connect sr-done signal (only once per source)
    {
        std::lock_guard lock(mutex_);
        auto& state = source_states_[source_id];
        if (state.signal_handler_id == 0) {
            state.signal_handler_id =
                g_signal_connect(urisrcbin, "sr-done",
                                 G_CALLBACK(&SmartRecordProbeHandler::on_recording_done), this);
            LOG_D("SmartRecord: connected sr-done signal for source {}", source_id);
        }
    }

    return urisrcbin;
}

// ── Recording Control ──────────────────────────────────────────────

bool SmartRecordProbeHandler::can_start_recording(uint32_t source_id, GstClockTime now) {
    std::lock_guard lock(mutex_);
    auto& state = source_states_[source_id];

    // Already recording this source?
    if (state.active_session)
        return false;

    // Max concurrent recordings (0 = unlimited)
    if (max_concurrent_recordings_ > 0) {
        // count_active_recordings() — caller holds mutex_
        if (count_active_recordings() >= max_concurrent_recordings_) {
            LOG_D("SmartRecord: max concurrent recordings ({}) reached, blocking source {}",
                  max_concurrent_recordings_, source_id);
            return false;
        }
    }

    // Min-interval check using GstClockTime arithmetic
    if (state.last_record_time != GST_CLOCK_TIME_NONE) {
        const GstClockTime min_interval_ns =
            static_cast<GstClockTime>(min_interval_sec_) * GST_SECOND;
        if (now - state.last_record_time < min_interval_ns)
            return false;
    }

    return true;
}

void SmartRecordProbeHandler::start_recording(uint32_t source_id, uint64_t trigger_object_id,
                                              GstClockTime now) {
    GstElement* urisrcbin = find_nvurisrcbin(source_id);
    if (!urisrcbin)
        return;

    const uint32_t total_duration = static_cast<uint32_t>(pre_event_sec_ + post_event_sec_);
    const uint32_t start_time_sec = static_cast<uint32_t>(pre_event_sec_);

    // Emit start-sr signal on nvurisrcbin
    guint sr_session_id = 0;
    g_signal_emit_by_name(urisrcbin, "start-sr", &sr_session_id, start_time_sec, total_duration,
                          nullptr);

    // Build session
    RecordingSession session;
    session.session_id = static_cast<uint32_t>(sr_session_id);
    session.source_id = source_id;
    session.source_name = get_source_name(source_id);
    session.start_time = now;
    session.duration_sec = total_duration;

    // actual_start = now - pre_event_ns: this is when the circular buffer data starts.
    // Using it as the interval reference prevents the next detection from starting a recording
    // while the pre-event portion of the current one is still being flushed.
    const GstClockTime pre_event_ns = static_cast<GstClockTime>(pre_event_sec_) * GST_SECOND;
    const GstClockTime actual_start = (now > pre_event_ns) ? (now - pre_event_ns) : 0;

    {
        std::lock_guard lock(mutex_);
        auto& state = source_states_[source_id];
        state.last_record_time = actual_start;
        state.active_session = session;
    }

    LOG_I("SmartRecord: started — session={}, source={} ({}), pre={}s, total={}s, trigger_obj={}",
          session.session_id, source_id, session.source_name, start_time_sec, total_duration,
          trigger_object_id);

    publish_record_started(source_id, session.source_name, session.session_id, trigger_object_id);
}

// ── Session Cleanup ────────────────────────────────────────────────

void SmartRecordProbeHandler::cleanup_expired_sessions(GstClockTime now) {
    std::lock_guard lock(mutex_);
    for (auto& [source_id, state] : source_states_) {
        if (!state.active_session)
            continue;
        const auto& session = *state.active_session;
        if (session.start_time == GST_CLOCK_TIME_NONE)
            continue;

        const GstClockTime expected_ns =
            static_cast<GstClockTime>(session.duration_sec) * GST_SECOND;
        const GstClockTime deadline = session.start_time + expected_ns + kSessionGracePeriodNs;

        if (now > deadline) {
            LOG_W(
                "SmartRecord: session {} (source {}) expired without sr-done "
                "(expected {}s + {}s grace) — clearing",
                session.session_id, source_id, session.duration_sec,
                kSessionGracePeriodNs / GST_SECOND);
            state.active_session.reset();
        }
    }
}

int SmartRecordProbeHandler::count_active_recordings() const {
    // Caller MUST hold mutex_
    int count = 0;
    for (const auto& [sid, state] : source_states_) {
        if (state.active_session)
            ++count;
    }
    return count;
}

// ── sr-done Callback ───────────────────────────────────────────────

void SmartRecordProbeHandler::on_recording_done(GstElement* nvurisrcbin, gpointer recording_info,
                                                gpointer /*recording_user_data*/,
                                                gpointer user_data) {
    if (!user_data)
        return;
    auto* self = static_cast<SmartRecordProbeHandler*>(user_data);

    // Guard against callbacks firing after destructor has been entered
    if (self->shutting_down_.load(std::memory_order_relaxed))
        return;

    // Determine source_id from qdata (preferred — survives cache eviction)
    uint32_t source_id = 0;
    bool found = false;

    if (nvurisrcbin) {
        gpointer sid_ptr = g_object_get_data(G_OBJECT(nvurisrcbin), kSourceIdDataKey);
        if (sid_ptr) {
            source_id = GPOINTER_TO_UINT(sid_ptr);
            found = true;
        }
    }

    // Fallback: linear search through cache
    if (!found) {
        for (const auto& [sid, elem] : self->source_bins_) {
            if (elem == nvurisrcbin) {
                source_id = sid;
                found = true;
                break;
            }
        }
    }

    if (!found) {
        LOG_E("SmartRecord: sr-done callback — source_id not found on element");
        return;
    }

    // Clear active session
    uint32_t session_id = 0;
    {
        std::lock_guard lock(self->mutex_);
        auto it = self->source_states_.find(source_id);
        if (it != self->source_states_.end() && it->second.active_session) {
            session_id = it->second.active_session->session_id;
            it->second.active_session.reset();
        }
    }

    // Extract info from NvDsSRRecordingInfo (sessionId, filename, dirpath, duration, width, height)
    auto* info = recording_info ? static_cast<NvDsSRRecordingInfo*>(recording_info) : nullptr;
    const std::string filename = (info && info->filename) ? info->filename : "unknown";

    LOG_I("SmartRecord: done — session={}, source={}, file='{}'", session_id, source_id, filename);

    self->publish_record_done(source_id, self->get_source_name(source_id), session_id, info);
}

// ── Message Publishing ─────────────────────────────────────────────

/**
 * @brief publish_record_started — JSON payload matching lantanav2 field names.
 *
 * Field mapping (compatible với consumer side):
 *   pid        : pipeline id
 *   sid        : source id (int)
 *   sname      : source name (camera id string)
 *   session_id : session id từ nvurisrcbin start-sr
 *   start_time : pre_event_sec — số giây back-fill từ circular buffer
 *   duration   : total (pre+post) duration tính bằng milliseconds
 *   trigger_obj: tracker object_id đã kích hoạt recording
 *   event_ts   : Unix epoch milliseconds
 */
void SmartRecordProbeHandler::publish_record_started(uint32_t source_id,
                                                     const std::string& source_name,
                                                     uint32_t session_id,
                                                     uint64_t trigger_object_id) {
    if (!producer_ || broker_channel_.empty())
        return;

    const uint32_t total_duration = static_cast<uint32_t>(pre_event_sec_ + post_event_sec_);
    try {
        json msg;
        msg["event"] = "record_started";
        msg["pid"] = pipeline_id_;
        msg["sid"] = source_id;
        msg["sname"] = source_name;
        msg["session_id"] = session_id;
        msg["start_time"] = pre_event_sec_;       // seconds: circular buffer back-fill depth
        msg["duration"] = total_duration * 1000;  // total duration in milliseconds
        msg["trigger_obj"] = trigger_object_id;
        msg["event_ts"] = now_epoch_ms();

        producer_->publish_json(broker_channel_, msg.dump());
    } catch (const std::exception& e) {
        LOG_W("SmartRecord: publish record_started failed: {}", e.what());
    }
}

/**
 * @brief publish_record_done — JSON payload matching lantanav2 field names.
 *
 * Field mapping:
 *   pid        : pipeline id
 *   sid        : source id (int)
 *   sname      : source name
 *   session_id : session id
 *   width      : video width from NvDsSRRecordingInfo
 *   height     : video height from NvDsSRRecordingInfo
 *   filename   : full file path from NvDsSRRecordingInfo (e.g.
 * "/opt/engine/data/rec/vms_rec_cam0_1234.mp4") duration   : raw NvDsSRRecordingInfo::duration
 * (nanoseconds from GStreamer / DeepStream) event_ts   : Unix epoch milliseconds
 */
void SmartRecordProbeHandler::publish_record_done(uint32_t source_id,
                                                  const std::string& source_name,
                                                  uint32_t session_id, NvDsSRRecordingInfo* info) {
    if (!producer_ || broker_channel_.empty())
        return;

    try {
        json msg;
        msg["event"] = "record_done";
        msg["pid"] = pipeline_id_;
        msg["sid"] = source_id;
        msg["sname"] = source_name;
        msg["session_id"] = session_id;
        msg["width"] = info ? static_cast<uint32_t>(info->width) : 0u;
        msg["height"] = info ? static_cast<uint32_t>(info->height) : 0u;
        msg["filename"] = (info && info->filename) ? info->filename : "";
        msg["duration"] = info ? info->duration : 0;  // nanoseconds from NvDsSRRecordingInfo
        msg["event_ts"] = now_epoch_ms();

        producer_->publish_json(broker_channel_, msg.dump());
    } catch (const std::exception& e) {
        LOG_W("SmartRecord: publish record_done failed: {}", e.what());
    }
}

// ── Helpers ────────────────────────────────────────────────────────

std::string SmartRecordProbeHandler::get_source_name(uint32_t source_id) const {
    const std::string runtime_source_name =
        engine::pipeline::lookup_runtime_source_name(source_root_, static_cast<int>(source_id));
    if (!runtime_source_name.empty()) {
        return runtime_source_name;
    }

    auto it = source_id_to_name_.find(static_cast<int>(source_id));
    if (it != source_id_to_name_.end())
        return it->second;
    return "source_" + std::to_string(source_id);
}

void SmartRecordProbeHandler::disconnect_all_signals() {
    std::lock_guard lock(mutex_);
    for (auto& [source_id, state] : source_states_) {
        if (state.signal_handler_id != 0) {
            auto it = source_bins_.find(source_id);
            if (it != source_bins_.end() && it->second) {
                g_signal_handler_disconnect(it->second, state.signal_handler_id);
            }
            state.signal_handler_id = 0;
        }
    }
}

}  // namespace engine::pipeline::probes
