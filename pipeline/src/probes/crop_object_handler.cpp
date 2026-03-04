#include "engine/pipeline/probes/crop_object_handler.hpp"
#include "engine/core/utils/logger.hpp"

#include <gstnvdsmeta.h>
#include <nvbufsurface.h>
#include <cuda_runtime.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace engine::pipeline::probes {

// ============================================================================
// Constants
// ============================================================================

/** @brief Emergency limit — all state maps cleared if any exceeds this. */
static constexpr size_t MAX_TRACKED_OBJECTS = 5000;

/** @brief Maximum label length for file names (after sanitization). */
static constexpr size_t MAX_LABEL_LENGTH = 30;

// ============================================================================
// Constructor / Destructor
// ============================================================================

CropObjectHandler::CropObjectHandler() {
    // Capture pipeline start time at construction (close to pipeline start).
    // Used for PTS -> absolute timestamp conversion.
    auto now = std::chrono::system_clock::now();
    pipeline_start_time_ns_ = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count());
}

CropObjectHandler::~CropObjectHandler() {
    shutting_down_.store(true, std::memory_order_release);

    if (enc_ctx_) {
        nvds_obj_enc_destroy_context(enc_ctx_);
        enc_ctx_ = nullptr;
        LOG_D("CropObjectHandler: encoder context destroyed");
    }

    LOG_I("CropObjectHandler: destroyed — final stats: object_keys={}, pub_states={}",
          object_keys_.size(), pub_state_.size());
}

// ============================================================================
// Configure
// ============================================================================

void CropObjectHandler::configure(const engine::core::config::PipelineConfig& config,
                                  const engine::core::config::EventHandlerConfig& handler,
                                  engine::core::messaging::IMessageProducer* producer) {
    pipeline_id_ = config.pipeline.id;
    label_filter_ = handler.label_filter;
    save_dir_base_ = handler.save_dir;
    image_quality_ = handler.image_quality;
    save_full_frame_ = handler.save_full_frame;
    producer_ = producer;

    // Convert capture_interval_sec to nanoseconds for PTS-based comparison
    capture_interval_ns_ = static_cast<uint64_t>(handler.capture_interval_sec) * GST_SECOND;

    // Pipeline frame dimensions (for publish metadata)
    pipeline_width_ = config.sources.width;
    pipeline_height_ = config.sources.height;

    // Cleanup config
    if (handler.cleanup) {
        stale_object_timeout_min_ = handler.cleanup->stale_object_timeout_min;
        check_interval_batches_ = handler.cleanup->check_interval_batches;
        old_dirs_max_days_ = handler.cleanup->old_dirs_max_days;
    }

    // Channel name: empty = no publish
    broker_channel_ = handler.channel;

    // Build source_id -> camera name lookup
    for (int i = 0; i < static_cast<int>(config.sources.cameras.size()); ++i) {
        source_id_to_name_[i] = config.sources.cameras[i].id;
    }

    // Create encoder context (GPU 0)
    enc_ctx_ = nvds_obj_enc_create_context(0);
    if (!enc_ctx_) {
        LOG_E("CropObjectHandler: failed to create NvDsObjEnc context");
    }

    // External processor service (optional — only created when configured)
    ext_proc_svc_.reset();
    if (handler.ext_processor && handler.ext_processor->enable &&
        !handler.ext_processor->rules.empty()) {
        ext_proc_svc_ = std::make_unique<ExternalProcessorService>();
        ext_proc_svc_->configure(*handler.ext_processor, pipeline_id_, producer, broker_channel_);
    }

    LOG_I(
        "CropObjectHandler: configured — labels={}, interval={}ns, quality={}, "
        "full_frame={}, dir='{}', broker='{}', old_dirs_max_days={}",
        label_filter_.size(), capture_interval_ns_, image_quality_, save_full_frame_,
        save_dir_base_, broker_channel_, old_dirs_max_days_);
}

// ============================================================================
// Static Probe Callback
// ============================================================================

GstPadProbeReturn CropObjectHandler::on_buffer(GstPad* /*pad*/, GstPadProbeInfo* info,
                                               gpointer user_data) {
    auto* self = static_cast<CropObjectHandler*>(user_data);

    // Early exit if handler is being destroyed
    if (self->shutting_down_.load(std::memory_order_acquire)) {
        return GST_PAD_PROBE_OK;
    }

    GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!buf)
        return GST_PAD_PROBE_OK;
    return self->process_batch(buf);
}

// ============================================================================
// Main Batch Processing
// ============================================================================

GstPadProbeReturn CropObjectHandler::process_batch(GstBuffer* buf) {
    NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buf);
    if (!batch_meta)
        return GST_PAD_PROBE_OK;

    // Map buffer to get NvBufSurface
    GstMapInfo map_info;
    if (!gst_buffer_map(buf, &map_info, GST_MAP_READ)) {
        LOG_E("CropObjectHandler: failed to map GstBuffer");
        return GST_PAD_PROBE_OK;
    }

    auto* ip_surf = reinterpret_cast<NvBufSurface*>(map_info.data);
    if (!ip_surf) {
        LOG_E("CropObjectHandler: mapped NvBufSurface is null");
        gst_buffer_unmap(buf, &map_info);
        return GST_PAD_PROBE_OK;
    }

    // CRITICAL: Synchronize CUDA device to ensure decoder has finished writing
    // to GPU buffer before encoder reads from it. Without this, partial/stale
    // buffer data may be read causing blurry or corrupted JPEG regions.
    cudaError_t cuda_err = cudaDeviceSynchronize();
    if (cuda_err != cudaSuccess) {
        LOG_W("CropObjectHandler: cudaDeviceSynchronize failed: {}", cudaGetErrorString(cuda_err));
        // Continue anyway — some frames may still be OK
    }

    // Ensure daily output directory
    if (!ensure_daily_dir()) {
        gst_buffer_unmap(buf, &map_info);
        return GST_PAD_PROBE_OK;
    }

    // Get buffer PTS as reference time for this batch
    GstClockTime batch_pts = GST_BUFFER_PTS(buf);
    int64_t timestamp_ms = now_epoch_ms();

    // Reset per-batch state
    full_frame_paths_this_batch_.clear();

    // Batch-accumulate pattern: collect all pending messages, then publish after
    // nvds_obj_enc_finish() ensures all JPEG files are written to disk.
    std::vector<PendingMessage> pending_messages;
    bool any_encoded = false;

    std::lock_guard<std::mutex> lock(mutex_);

    // Check encoder context
    if (!enc_ctx_) {
        LOG_E("CropObjectHandler: encoder context null, aborting batch");
        gst_buffer_unmap(buf, &map_info);
        return GST_PAD_PROBE_OK;
    }

    for (NvDsMetaList* l_frame = batch_meta->frame_meta_list; l_frame; l_frame = l_frame->next) {
        auto* frame_meta = static_cast<NvDsFrameMeta*>(l_frame->data);
        if (!frame_meta)
            continue;

        int src_id = static_cast<int>(frame_meta->source_id);

        // Use frame PTS if valid, otherwise fall back to buffer PTS
        GstClockTime frame_pts =
            GST_CLOCK_TIME_IS_VALID(frame_meta->buf_pts) ? frame_meta->buf_pts : batch_pts;

        // Get source frame dimensions from NvBufSurface
        int src_frame_w = 0;
        int src_frame_h = 0;
        if (frame_meta->batch_id < ip_surf->numFilled) {
            src_frame_w = static_cast<int>(ip_surf->surfaceList[frame_meta->batch_id].width);
            src_frame_h = static_cast<int>(ip_surf->surfaceList[frame_meta->batch_id].height);
        }

        // Generate wall-clock timestamp string for file naming (once per frame)
        std::string rt_str = generate_realtime_str();

        for (NvDsMetaList* l_obj = frame_meta->obj_meta_list; l_obj; l_obj = l_obj->next) {
            auto* obj = static_cast<NvDsObjectMeta*>(l_obj->data);
            if (!obj)
                continue;

            const std::string label(obj->obj_label);

            // Extract SGIE classifier labels (empty string if no SGIE running)
            std::string sgie_labels = build_sgie_labels(obj);

            // Label filter check
            if (!label_filter_.empty()) {
                auto fit = std::find(label_filter_.begin(), label_filter_.end(), label);
                if (fit == label_filter_.end())
                    continue;
            }

            uint64_t tracker_id = obj->object_id;
            uint64_t key = compose_key(src_id, tracker_id);

            // Always update last_seen (even when not capturing)
            update_object_last_seen(src_id, tracker_id, frame_pts);

            // Determine publish decision
            PubDecisionType decision = decide_capture(key, frame_pts, sgie_labels);
            if (decision == PubDecisionType::None) {
                continue;
            }

            // Compute payload hash for heartbeat dedup
            float left = obj->rect_params.left;
            float top = obj->rect_params.top;
            float width = obj->rect_params.width;
            float height = obj->rect_params.height;

            std::size_t payload_hash =
                compute_payload_hash(obj->class_id, label, sgie_labels, left, top, width, height);

            // Heartbeat dedup: skip if payload unchanged since last publish
            auto& pub = pub_state_[key];
            if (decision == PubDecisionType::Heartbeat && payload_hash == pub.last_payload_hash) {
                LOG_T("CropObjectHandler: heartbeat dedup suppressed src={} tid={}", src_id,
                      tracker_id);
                continue;
            }

            // --- Commit: this object will be captured and published ---

            std::string object_key = get_or_create_object_key(src_id, tracker_id);
            std::string instance_key = uuid_gen_.generate();
            last_capture_pts_[key] = frame_pts;

            // Update pub state
            std::string prev_mid = pub.last_message_id;
            std::string message_id =
                generate_message_id(src_id, frame_meta->frame_num, timestamp_ms);
            uint64_t hb_seq = 0;

            if (decision == PubDecisionType::FirstSeen) {
                pub.heartbeat_seq = 0;
                hb_seq = 0;
            } else if (decision == PubDecisionType::Heartbeat) {
                pub.heartbeat_seq++;
                hb_seq = pub.heartbeat_seq;
            } else {
                // Bypass: use current seq without incrementing
                hb_seq = pub.heartbeat_seq;
            }
            pub.last_publish_pts = frame_pts;
            pub.last_payload_hash = payload_hash;
            pub.last_message_id = message_id;
            pub.last_sgie_labels = sgie_labels;  // Update after commit (baseline for next bypass)

            // -- Crop Object -------------------------------------------------
            // File naming: s{sid}_RT{realtime}_{label}_id{oid}.jpg
            // Directory: {save_dir_base}/{YYYYMMDD}/ (flat, no src_N/ subdir)
            std::string safe_label = sanitize_label(label);
            std::string crop_filename =
                fmt::format("s{}_RT{}_{}_id{}.jpg", src_id, rt_str, safe_label, tracker_id);
            std::string crop_path = current_day_dir_ + "/" + crop_filename;

            {
                NvDsObjEncUsrArgs enc_args = {};
                enc_args.saveImg = TRUE;
                enc_args.attachUsrMeta = FALSE;
                enc_args.scaleImg = FALSE;
                enc_args.scaledWidth = 0;
                enc_args.scaledHeight = 0;
                enc_args.quality = image_quality_;
                enc_args.isFrame = 0;  // Crop object, not full frame
                std::snprintf(enc_args.fileNameImg, sizeof(enc_args.fileNameImg), "%s",
                              crop_path.c_str());

                if (nvds_obj_enc_process(enc_ctx_, &enc_args, ip_surf, obj, frame_meta)) {
                    any_encoded = true;
                } else {
                    LOG_W("CropObjectHandler: nvds_obj_enc_process failed for crop src={} tid={}",
                          src_id, tracker_id);
                }
            }

            // -- Full Frame (once per source per batch) ----------------------
            // File naming: s{sid}_RT{realtime}_frame_ff.jpg (lantanav2 aligned)
            std::string ff_path;
            uint64_t frame_num_key = static_cast<uint64_t>(frame_meta->frame_num);

            if (save_full_frame_) {
                auto ff_it = full_frame_paths_this_batch_.find(frame_num_key);
                if (ff_it != full_frame_paths_this_batch_.end()) {
                    // Already captured for this frame number
                    ff_path = ff_it->second;
                } else {
                    std::string ff_filename = fmt::format("s{}_RT{}_frame_ff.jpg", src_id, rt_str);
                    ff_path = current_day_dir_ + "/" + ff_filename;

                    NvDsObjEncUsrArgs ff_args = {};
                    ff_args.saveImg = TRUE;
                    ff_args.attachUsrMeta = FALSE;
                    ff_args.scaleImg = FALSE;
                    ff_args.quality = image_quality_;
                    ff_args.isFrame = 1;  // Full frame
                    std::snprintf(ff_args.fileNameImg, sizeof(ff_args.fileNameImg), "%s",
                                  ff_path.c_str());

                    if (nvds_obj_enc_process(enc_ctx_, &ff_args, ip_surf, obj, frame_meta)) {
                        full_frame_paths_this_batch_[frame_num_key] = ff_path;
                        any_encoded = true;
                    } else {
                        ff_path.clear();
                    }
                }
            }

            // -- Accumulate Pending Message ----------------------------------
            std::string source_name;
            auto name_it = source_id_to_name_.find(src_id);
            if (name_it != source_id_to_name_.end()) {
                source_name = name_it->second;
            } else {
                source_name = fmt::format("source_{}", src_id);
            }

            pending_messages.push_back(PendingMessage{
                src_id,
                source_name,
                object_key,
                instance_key,
                obj->class_id,
                label,
                std::move(sgie_labels),  // labels: SGIE classifier results
                obj->confidence,
                tracker_id,
                left,
                top,
                width,
                height,
                relative_path(crop_path),  // fname (relative to save_dir_base)
                relative_path(ff_path),    // fname_ff (relative to save_dir_base)
                timestamp_ms,
                static_cast<uint64_t>(frame_meta->frame_num),
                message_id,
                prev_mid,
                decision,
                pub_reason_for_type(decision),  // pub_reason string
                hb_seq,
                src_frame_w,      // source_frame_width
                src_frame_h,      // source_frame_height
                pipeline_width_,  // pipeline_width
                pipeline_height_  // pipeline_height
            });

            // -- External Processing (label-rule-based HTTP enrichment) -----
            // MUST run while ip_surf is still mapped (before nvds_obj_enc_finish).
            // The service uses its own NvDsObjEnc context (saveImg=FALSE),
            // encodes in-memory JPEG, and dispatches a non-blocking API call.
            if (ext_proc_svc_) {
                ext_proc_svc_->process_object(obj, frame_meta, ip_surf, source_name, instance_key,
                                              object_key);
            }
        }
    }

    // CRITICAL: Always call nvds_obj_enc_finish every batch to release accumulated
    // CUDA surfaces, even if no encodes happened this batch. This ensures all
    // JPEG files are written to disk BEFORE we publish messages referencing them.
    nvds_obj_enc_finish(enc_ctx_);

    gst_buffer_unmap(buf, &map_info);

    // Publish all accumulated messages AFTER nvds_obj_enc_finish
    if (!pending_messages.empty()) {
        publish_pending_messages(pending_messages);
    }

    // Periodic maintenance
    ++batch_counter_;
    if (check_interval_batches_ > 0 && batch_counter_ % check_interval_batches_ == 0) {
        GstClockTime cleanup_ref =
            GST_CLOCK_TIME_IS_VALID(batch_pts) ? batch_pts : static_cast<GstClockTime>(0);
        // cleanup_stale_objects frees memory for stale objects.
        // Exit messages are intentionally NOT published to the broker.
        cleanup_stale_objects(cleanup_ref, timestamp_ms);
        cleanup_old_directories();
        log_memory_stats();
    }

    return GST_PAD_PROBE_OK;
}

// ============================================================================
// Daily Directory Management
// ============================================================================

bool CropObjectHandler::ensure_daily_dir() {
    int today = today_yyyymmdd();
    if (today == current_date_yyyymmdd_ && !current_day_dir_.empty()) {
        return true;  // Already set up for today
    }

    current_date_yyyymmdd_ = today;
    current_day_dir_ = save_dir_base_ + "/" + format_date_dir(today);

    std::error_code ec;
    fs::create_directories(current_day_dir_, ec);
    if (ec) {
        LOG_E("CropObjectHandler: failed to create dir '{}': {}", current_day_dir_, ec.message());
        current_day_dir_ = save_dir_base_;  // Fallback to base directory
        return false;
    }

    LOG_I("CropObjectHandler: daily dir rotated to '{}'", current_day_dir_);
    return true;
}

void CropObjectHandler::cleanup_old_directories() {
    if (old_dirs_max_days_ <= 0)
        return;

    // Rate limit: at most once per hour
    int64_t now_sec = std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
    if (last_old_dir_cleanup_epoch_ > 0 && (now_sec - last_old_dir_cleanup_epoch_) < 3600) {
        return;
    }
    last_old_dir_cleanup_epoch_ = now_sec;

    LOG_I("CropObjectHandler: running old directory cleanup (older than {} days)",
          old_dirs_max_days_);

    try {
        if (!fs::exists(save_dir_base_) || !fs::is_directory(save_dir_base_)) {
            LOG_W("CropObjectHandler: base dir '{}' does not exist, skipping cleanup",
                  save_dir_base_);
            return;
        }

        // Compute threshold date as YYYYMMDD integer
        std::time_t now_c = std::time(nullptr);
        auto threshold_tp = std::chrono::system_clock::from_time_t(now_c) -
                            std::chrono::hours(24 * old_dirs_max_days_);
        std::time_t threshold_c = std::chrono::system_clock::to_time_t(threshold_tp);

        std::tm threshold_tm{};
        localtime_r(&threshold_c, &threshold_tm);
        int threshold_yyyymmdd = (threshold_tm.tm_year + 1900) * 10000 +
                                 (threshold_tm.tm_mon + 1) * 100 + threshold_tm.tm_mday;

        for (const auto& entry : fs::directory_iterator(save_dir_base_)) {
            if (!entry.is_directory())
                continue;

            std::string dirname = entry.path().filename().string();
            // Match YYYYMMDD format (8 digits)
            if (dirname.length() != 8 || !std::all_of(dirname.begin(), dirname.end(), ::isdigit)) {
                continue;
            }

            try {
                int dir_date = std::stoi(dirname);
                if (dir_date < threshold_yyyymmdd) {
                    LOG_I("CropObjectHandler: deleting old dir: {}", entry.path().string());
                    std::error_code ec;
                    fs::remove_all(entry.path(), ec);
                    if (ec) {
                        LOG_E("CropObjectHandler: error deleting dir '{}': {}",
                              entry.path().string(), ec.message());
                    }
                }
            } catch (...) {
                // Invalid dirname format, skip
            }
        }
    } catch (const std::exception& e) {
        LOG_E("CropObjectHandler: exception during old dir cleanup: {}", e.what());
    }
}

// ============================================================================
// SGIE Label Extraction
// ============================================================================

std::string CropObjectHandler::build_sgie_labels(NvDsObjectMeta* obj) {
    if (!obj || !obj->classifier_meta_list)
        return "";

    std::ostringstream oss;
    bool first = true;

    for (NvDsMetaList* l_cls = obj->classifier_meta_list; l_cls; l_cls = l_cls->next) {
        auto* cls = static_cast<NvDsClassifierMeta*>(l_cls->data);
        if (!cls)
            continue;
        for (NvDsMetaList* l_lbl = cls->label_info_list; l_lbl; l_lbl = l_lbl->next) {
            auto* lbl = static_cast<NvDsLabelInfo*>(l_lbl->data);
            if (!lbl)
                continue;
            if (!first)
                oss << "|";
            first = false;
            // Format: result_label:label_id:result_prob  (matches lantanav2)
            oss << (lbl->result_label ? lbl->result_label : "") << ":" << lbl->label_id << ":"
                << std::fixed << std::setprecision(2) << lbl->result_prob;
        }
    }
    return oss.str();
}

// ============================================================================
// Capture Decision (PTS-based) — Bypass on SGIE Label Change
// ============================================================================

PubDecisionType CropObjectHandler::decide_capture(uint64_t key, GstClockTime current_pts,
                                                  const std::string& sgie_labels) {
    // First-time: new object, always capture.
    // Seed last_sgie_labels so the first subsequent bypass check has a baseline.
    auto cap_it = last_capture_pts_.find(key);
    if (cap_it == last_capture_pts_.end()) {
        auto& state = pub_state_[key];
        state.bypass_tokens = BURST_MAX;
        state.last_sgie_labels = sgie_labels;  // Baseline — bypass fires on CHANGE from this
        if (GST_CLOCK_TIME_IS_VALID(current_pts)) {
            state.last_refill_pts = current_pts;
        }
        return PubDecisionType::FirstSeen;
    }

    // No throttling configured: treat every frame as heartbeat
    if (capture_interval_ns_ == 0) {
        return PubDecisionType::Heartbeat;
    }

    GstClockTime last_pts = cap_it->second;

    // Invalid PTS — allow capture as heartbeat (safety fallback)
    if (!GST_CLOCK_TIME_IS_VALID(last_pts) || !GST_CLOCK_TIME_IS_VALID(current_pts)) {
        return PubDecisionType::Heartbeat;
    }

    uint64_t elapsed = (current_pts >= last_pts) ? (current_pts - last_pts) : 0;

    // Check if capture interval has elapsed → Heartbeat
    if (elapsed >= capture_interval_ns_) {
        auto& state = pub_state_[key];
        refill_tokens(state, current_pts);
        return PubDecisionType::Heartbeat;
    }

    // Within capture interval — bypass only on SGIE label change (lantanav2 LabelStateV2)
    // Empty sgie_labels means no SGIE running — no bypass possible
    auto& state = pub_state_[key];
    refill_tokens(state, current_pts);
    if (!sgie_labels.empty() && sgie_labels != state.last_sgie_labels && attempt_bypass(state)) {
        return PubDecisionType::Bypass;
    }

    return PubDecisionType::None;
}

// ============================================================================
// Payload Hash (for Heartbeat Dedup)
// ============================================================================

std::size_t CropObjectHandler::compute_payload_hash(int class_id, const std::string& label,
                                                    const std::string& sgie_labels, float left,
                                                    float top, float width, float height) {
    // Quantize bbox to integer pixels to avoid floating-point jitter
    // causing hash mismatches for effectively identical positions.
    int q_left = static_cast<int>(left);
    int q_top = static_cast<int>(top);
    int q_width = static_cast<int>(width);
    int q_height = static_cast<int>(height);

    std::size_t seed = std::hash<int>{}(class_id);
    seed ^= std::hash<std::string>{}(label) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= std::hash<std::string>{}(sgie_labels) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= std::hash<int>{}(q_left) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= std::hash<int>{}(q_top) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= std::hash<int>{}(q_width) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= std::hash<int>{}(q_height) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    return seed;
}

// ============================================================================
// Object Key Management
// ============================================================================

uint64_t CropObjectHandler::compose_key(int source_id, uint64_t tracker_id) {
    return (static_cast<uint64_t>(source_id) << 48) | (tracker_id & 0xFFFFFFFFFFFF);
}

std::string CropObjectHandler::get_or_create_object_key(int source_id, uint64_t tracker_id) {
    uint64_t key = compose_key(source_id, tracker_id);
    auto it = object_keys_.find(key);
    if (it != object_keys_.end()) {
        return it->second;
    }
    std::string new_key = uuid_gen_.generate();
    object_keys_[key] = new_key;
    return new_key;
}

void CropObjectHandler::update_object_last_seen(int source_id, uint64_t tracker_id,
                                                GstClockTime pts) {
    uint64_t key = compose_key(source_id, tracker_id);
    object_last_seen_[key] = pts;
}

// ============================================================================
// Stale Object Cleanup — generates Exit messages for removed objects
// ============================================================================

std::vector<CropObjectHandler::PendingMessage> CropObjectHandler::cleanup_stale_objects(
    GstClockTime current_pts, int64_t timestamp_ms) {
    std::vector<PendingMessage> exit_messages;

    // Convert timeout to nanoseconds for PTS comparison
    uint64_t timeout_ns = static_cast<uint64_t>(stale_object_timeout_min_) * 60ULL * GST_SECOND;
    if (timeout_ns == 0)
        return exit_messages;

    // If PTS is invalid, we can't do PTS-based cleanup
    if (!GST_CLOCK_TIME_IS_VALID(current_pts)) {
        LOG_D("CropObjectHandler: skipping stale cleanup (invalid PTS)");
        return exit_messages;
    }

    int removed = 0;
    for (auto it = object_last_seen_.begin(); it != object_last_seen_.end();) {
        GstClockTime last_seen = it->second;
        bool is_stale = false;

        if (!GST_CLOCK_TIME_IS_VALID(last_seen)) {
            is_stale = true;  // Invalid PTS entry — clean up
        } else if (current_pts > last_seen && (current_pts - last_seen) > timeout_ns) {
            is_stale = true;
        }

        if (is_stale) {
            uint64_t key = it->first;

            // Extract source_id and tracker_id from composed key
            int exit_src_id = static_cast<int>(key >> 48);
            uint64_t exit_tracker_id = key & 0xFFFFFFFFFFFFULL;

            // Look up object_key before erasing
            std::string exit_object_key;
            auto okey_it = object_keys_.find(key);
            if (okey_it != object_keys_.end()) {
                exit_object_key = okey_it->second;
            }

            // Look up source name
            std::string exit_source_name;
            auto name_it = source_id_to_name_.find(exit_src_id);
            if (name_it != source_id_to_name_.end()) {
                exit_source_name = name_it->second;
            } else {
                exit_source_name = fmt::format("source_{}", exit_src_id);
            }

            // Get prev_mid from pub state before clearing
            std::string exit_prev_mid;
            auto ps_it = pub_state_.find(key);
            if (ps_it != pub_state_.end()) {
                exit_prev_mid = ps_it->second.last_message_id;
            }

            // Generate Exit PendingMessage (metadata-only, no image paths)
            std::string exit_mid = generate_message_id(exit_src_id, 0, timestamp_ms);

            PendingMessage exit_msg{};
            exit_msg.source_id = exit_src_id;
            exit_msg.source_name = exit_source_name;
            exit_msg.object_key = exit_object_key;
            exit_msg.instance_key = "";  // No new instance for Exit
            exit_msg.class_id = 0;
            exit_msg.label = "";
            exit_msg.confidence = 0.0f;
            exit_msg.tracker_id = exit_tracker_id;
            exit_msg.left = 0;
            exit_msg.top = 0;
            exit_msg.width = 0;
            exit_msg.height = 0;
            exit_msg.fname = "";
            exit_msg.fname_ff = "";
            exit_msg.timestamp_ms = timestamp_ms;
            exit_msg.frame_num = 0;
            exit_msg.message_id = exit_mid;
            exit_msg.prev_message_id = exit_prev_mid;
            exit_msg.pub_type = PubDecisionType::Exit;
            exit_msg.pub_reason = pub_reason_for_type(PubDecisionType::Exit);
            exit_msg.heartbeat_seq = 0;
            exit_msg.source_frame_width = 0;
            exit_msg.source_frame_height = 0;
            exit_msg.pipeline_width = pipeline_width_;
            exit_msg.pipeline_height = pipeline_height_;

            exit_messages.push_back(std::move(exit_msg));

            // Erase all state for this object
            object_keys_.erase(key);
            last_capture_pts_.erase(key);
            pub_state_.erase(key);
            it = object_last_seen_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }

    // Emergency map size limit (prevent unbounded growth from tracker ID churn)
    size_t max_map = std::max({object_keys_.size(), object_last_seen_.size(),
                               last_capture_pts_.size(), pub_state_.size()});
    if (max_map > MAX_TRACKED_OBJECTS) {
        LOG_W("CropObjectHandler: EMERGENCY — map size {} exceeded limit {}, clearing all", max_map,
              MAX_TRACKED_OBJECTS);
        object_keys_.clear();
        object_last_seen_.clear();
        last_capture_pts_.clear();
        pub_state_.clear();
    }

    if (removed > 0) {
        LOG_D("CropObjectHandler: stale cleanup removed {} objects ({} exit msgs), {} remaining",
              removed, exit_messages.size(), object_keys_.size());
    }

    return exit_messages;
}

// ============================================================================
// Memory Stats Logging
// ============================================================================

void CropObjectHandler::log_memory_stats() const {
    // Approximate memory usage per map entry (key overhead + value)
    // object_keys_:       8 (key) + ~56 (string SSO or heap) = ~64 bytes
    // object_last_seen_:  8 (key) + 8 (GstClockTime)         = ~16 bytes
    // last_capture_pts_:  8 (key) + 8 (GstClockTime)         = ~16 bytes
    // pub_state_:         8 (key) + ~80 (ObjectPubState)       = ~88 bytes

    size_t n_keys = object_keys_.size();
    size_t n_seen = object_last_seen_.size();
    size_t n_pts = last_capture_pts_.size();
    size_t n_pub = pub_state_.size();

    size_t approx_kb = (n_keys * 64 + n_seen * 16 + n_pts * 16 + n_pub * 88) / 1024;

    LOG_I(
        "CropObjectHandler: memory stats — "
        "object_keys={}, last_seen={}, capture_pts={}, pub_state={}, ~{}KB",
        n_keys, n_seen, n_pts, n_pub, approx_kb);
}

// ============================================================================
// Message ID Generation
// ============================================================================

std::string CropObjectHandler::generate_message_id(int source_id, uint64_t frame_num,
                                                   int64_t timestamp_ms) {
    // Compact format: src_<source_id>_f<frame>_<timestamp_ms>
    // Unique enough for correlation without full UUID overhead per message.
    return fmt::format("src_{}_f{}_{}", source_id, frame_num, timestamp_ms);
}

// ============================================================================
// Batch Message Publishing
// ============================================================================

void CropObjectHandler::publish_pending_messages(const std::vector<PendingMessage>& messages) {
    if (!producer_ || broker_channel_.empty())
        return;

    for (const auto& m : messages) {
        try {
            json msg;

            // -- lantanav2-aligned field names (crop_bb event) ---------------
            msg["event"] = "crop_bb";
            msg["pid"] = pipeline_id_;
            msg["sid"] = m.source_id;
            msg["sname"] = m.source_name;

            // Object identity
            msg["instance_key"] = m.instance_key;
            msg["oid"] = m.tracker_id;
            msg["object_key"] = m.object_key;

            // Parent fields — empty placeholders (future SGIE support)
            msg["parent_object_key"] = "";
            msg["parent"] = "";
            msg["parent_instance_key"] = "";

            // Classification
            msg["class"] = m.label;
            msg["conf"] = m.confidence;
            msg["labels"] = m.labels;  // SGIE classifier results (empty string if no SGIE)

            // Bounding box — flat fields (not nested)
            msg["top"] = m.top;
            msg["left"] = m.left;
            msg["w"] = m.width;
            msg["h"] = m.height;

            // Source frame & pipeline dimensions
            msg["s_w_ff"] = m.source_frame_width;
            msg["s_h_ff"] = m.source_frame_height;
            msg["w_ff"] = m.pipeline_width;
            msg["h_ff"] = m.pipeline_height;

            // File paths (relative to save_dir_base)
            msg["fname"] = m.fname;
            msg["fname_ff"] = m.fname_ff;

            // Timestamp as Unix ms string (lantanav2 convention)
            msg["event_ts"] = std::to_string(m.timestamp_ms);

            // Message chain & publish decision
            msg["mid"] = m.message_id;
            msg["prev_mid"] = m.prev_message_id;
            msg["pub_type"] = pub_type_name(m.pub_type);
            msg["pub_reason"] = m.pub_reason;
            msg["hb_seq"] = m.heartbeat_seq;

            // Extra fields (vms-engine additions — backward-compatible)
            msg["class_id"] = m.class_id;
            msg["frame_num"] = m.frame_num;
            msg["tracker_id"] = m.tracker_id;

            producer_->publish_json(broker_channel_, msg.dump());
        } catch (const std::exception& e) {
            LOG_W("CropObjectHandler: publish failed for tid={}: {}", m.tracker_id, e.what());
        }
    }

    LOG_T("CropObjectHandler: published {} messages to '{}'", messages.size(), broker_channel_);
}

// ============================================================================
// Label Sanitization
// ============================================================================

std::string CropObjectHandler::sanitize_label(const std::string& label) {
    std::string result;
    result.reserve(std::min(label.size(), MAX_LABEL_LENGTH));

    for (size_t i = 0; i < label.size() && result.size() < MAX_LABEL_LENGTH; ++i) {
        char c = label[i];
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
            result += c;
        } else {
            result += '_';
        }
    }

    return result.empty() ? "unknown" : result;
}

// ============================================================================
// Utility
// ============================================================================

int CropObjectHandler::today_yyyymmdd() {
    std::time_t now_c = std::time(nullptr);
    std::tm ltm{};
    localtime_r(&now_c, &ltm);
    return (ltm.tm_year + 1900) * 10000 + (ltm.tm_mon + 1) * 100 + ltm.tm_mday;
}

std::string CropObjectHandler::format_date_dir(int yyyymmdd) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08d", yyyymmdd);
    return std::string(buf);
}

int64_t CropObjectHandler::now_epoch_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

const char* CropObjectHandler::pub_type_name(PubDecisionType type) {
    switch (type) {
        case PubDecisionType::FirstSeen:
            return "first_seen";
        case PubDecisionType::Bypass:
            return "bypass";
        case PubDecisionType::Heartbeat:
            return "heartbeat";
        case PubDecisionType::Exit:
            return "exit";
        case PubDecisionType::None:
            return "none";
        default:
            return "unknown";
    }
}

const char* CropObjectHandler::pub_reason_for_type(PubDecisionType type) {
    switch (type) {
        case PubDecisionType::FirstSeen:
            return "first_detection";
        case PubDecisionType::Bypass:
            return "burst_capture";
        case PubDecisionType::Heartbeat:
            return "interval";
        case PubDecisionType::Exit:
            return "stale_expired";
        default:
            return "";
    }
}

std::string CropObjectHandler::generate_realtime_str() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm{};
    localtime_r(&now_c, &now_tm);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::ostringstream oss;
    oss << std::put_time(&now_tm, "%Y%m%d_%H%M%S");
    oss << "_" << std::setw(3) << std::setfill('0') << ms.count();
    return oss.str();
}

std::string CropObjectHandler::relative_path(const std::string& absolute_path) const {
    if (absolute_path.empty() || save_dir_base_.empty()) {
        return absolute_path;
    }
    // Strip save_dir_base_ prefix and leading slash
    if (absolute_path.size() > save_dir_base_.size() &&
        absolute_path.compare(0, save_dir_base_.size(), save_dir_base_) == 0) {
        size_t start = save_dir_base_.size();
        if (start < absolute_path.size() && absolute_path[start] == '/') {
            ++start;
        }
        return absolute_path.substr(start);
    }
    return absolute_path;  // Fallback: return as-is
}

// ============================================================================
// Token Bucket (Bypass)
// ============================================================================

void CropObjectHandler::refill_tokens(ObjectPubState& state, GstClockTime now_pts) {
    if (!GST_CLOCK_TIME_IS_VALID(now_pts)) {
        return;
    }

    if (!GST_CLOCK_TIME_IS_VALID(state.last_refill_pts)) {
        // First call for this object — initialize
        state.last_refill_pts = now_pts;
        state.bypass_tokens = BURST_MAX;
        return;
    }

    if (now_pts <= state.last_refill_pts) {
        return;  // PTS hasn't advanced
    }

    uint64_t elapsed = now_pts - state.last_refill_pts;
    if (elapsed >= TOKEN_REFILL_NS) {
        int tokens_to_add = static_cast<int>(elapsed / TOKEN_REFILL_NS);
        state.bypass_tokens = std::min(state.bypass_tokens + tokens_to_add, BURST_MAX);
        state.last_refill_pts = now_pts;
    }
}

bool CropObjectHandler::attempt_bypass(ObjectPubState& state) {
    if (state.bypass_tokens > 0) {
        state.bypass_tokens--;
        return true;
    }
    return false;
}

}  // namespace engine::pipeline::probes
