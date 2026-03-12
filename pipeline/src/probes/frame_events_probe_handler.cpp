#include "engine/pipeline/probes/frame_events_probe_handler.hpp"

#include "engine/core/messaging/imessage_producer.hpp"
#include "engine/core/utils/logger.hpp"
#include "engine/pipeline/evidence/frame_evidence_cache.hpp"
#include "engine/pipeline/extproc/frame_events_ext_proc_service.hpp"
#include "engine/pipeline/source_identity_registry.hpp"

#include <gstnvdsmeta.h>
#include <nlohmann/json.hpp>
#include <nvbufsurface.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

using json = nlohmann::json;

namespace engine::pipeline::probes {

namespace {

constexpr uint64_t kUntrackedObjectId = static_cast<uint64_t>(-1);

int64_t now_epoch_ms() {
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

std::string make_object_key(const std::string& pipeline_id, const std::string& source_name,
                            uint64_t object_id) {
    return pipeline_id + ":" + source_name + ":" + std::to_string(object_id);
}

std::string make_frame_key(const std::string& pipeline_id, const std::string& source_name,
                           uint64_t frame_num, int64_t frame_ts_ms) {
    return pipeline_id + ":" + source_name + ":" + std::to_string(frame_num) + ":" +
           std::to_string(frame_ts_ms);
}

std::string sanitize_ref_component(const std::string& value) {
    std::string sanitized;
    sanitized.reserve(value.size());
    for (const char ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) || ch == '-' || ch == '_' || ch == '.') {
            sanitized.push_back(ch);
        } else {
            sanitized.push_back('_');
        }
    }
    return sanitized.empty() ? std::string("unknown") : sanitized;
}

std::string build_overview_ref(const engine::pipeline::evidence::FrameCaptureMetadata& meta) {
    const std::string safe_pipeline = sanitize_ref_component(meta.pipeline_id);
    const std::string safe_source_name = sanitize_ref_component(meta.source_name);
    // Publish only the flat filename/ref. save_dir is applied later by the
    // evidence materialization path when writing the JPEG to disk.
    return safe_pipeline + "_" + safe_source_name + "_" + std::to_string(meta.frame_num) + "_" +
           std::to_string(meta.frame_ts_ms) + "_overview.jpg";
}

std::string build_crop_ref(const engine::pipeline::evidence::FrameCaptureMetadata& meta,
                           const FrameEventObject& object) {
    const std::string safe_pipeline = sanitize_ref_component(meta.pipeline_id);
    const std::string safe_source_name = sanitize_ref_component(meta.source_name);
    // Publish only the flat filename/ref. save_dir is applied later by the
    // evidence materialization path when writing the JPEG to disk.
    return safe_pipeline + "_" + safe_source_name + "_" + std::to_string(meta.frame_num) + "_" +
           std::to_string(meta.frame_ts_ms) + "_crop_" + std::to_string(object.object_id) + ".jpg";
}

std::vector<std::string> collect_sgie_labels(NvDsObjectMeta* object_meta) {
    std::vector<std::string> labels;
    if (!object_meta || !object_meta->classifier_meta_list) {
        return labels;
    }

    for (NvDsMetaList* classifier_iter = object_meta->classifier_meta_list; classifier_iter;
         classifier_iter = classifier_iter->next) {
        auto* classifier_meta = static_cast<NvDsClassifierMeta*>(classifier_iter->data);
        if (!classifier_meta) {
            continue;
        }

        for (NvDsMetaList* label_iter = classifier_meta->label_info_list; label_iter;
             label_iter = label_iter->next) {
            auto* label_info = static_cast<NvDsLabelInfo*>(label_iter->data);
            if (!label_info) {
                continue;
            }

            std::ostringstream oss;
            oss << (label_info->result_label ? label_info->result_label : "") << ":"
                << label_info->label_id << ":" << std::fixed << std::setprecision(2)
                << label_info->result_prob;
            labels.push_back(oss.str());
        }
    }

    return labels;
}

std::string build_label_signature(const std::vector<std::string>& labels) {
    if (labels.empty()) {
        return "";
    }

    std::vector<std::string> parts = labels;
    for (auto& part : parts) {
        const size_t separator = part.find(':');
        if (separator != std::string::npos) {
            part = part.substr(0, separator);
        }
    }
    std::sort(parts.begin(), parts.end());

    std::ostringstream joined;
    for (size_t index = 0; index < parts.size(); ++index) {
        if (index > 0) {
            joined << '|';
        }
        joined << parts[index];
    }
    return joined.str();
}

// Build a stable signature for the object set on one frame.
// Sorting makes it resilient to metadata iteration order changes.
std::string build_signature(const std::vector<FrameEventObject>& objects) {
    std::vector<std::string> parts;
    parts.reserve(objects.size());
    for (const auto& object : objects) {
        std::ostringstream oss;
        oss << object.object_id << '|' << object.class_id << '|' << object.object_type << '|'
            << object.parent_object_id;
        parts.push_back(oss.str());
    }
    std::sort(parts.begin(), parts.end());

    std::ostringstream joined;
    for (size_t index = 0; index < parts.size(); ++index) {
        if (index > 0) {
            joined << ';';
        }
        joined << parts[index];
    }
    return joined.str();
}

}  // namespace

FrameEventsProbeHandler::~FrameEventsProbeHandler() = default;

void FrameEventsProbeHandler::configure(const engine::core::config::PipelineConfig& config,
                                        const engine::core::config::EventHandlerConfig& handler,
                                        GstElement* source_root,
                                        engine::core::messaging::IMessageProducer* producer,
                                        engine::pipeline::evidence::FrameEvidenceCache* cache) {
    pipeline_id_ = config.pipeline.id;
    handler_id_ = handler.id;
    source_width_ = config.sources.width;
    source_height_ = config.sources.height;
    broker_channel_ = handler.channel;
    label_filter_ = handler.label_filter;
    producer_ = producer;
    cache_ = cache;
    source_root_ = source_root;
    ext_proc_service_.reset();
    if (handler.frame_events) {
        config_ = *handler.frame_events;
    }

    if (producer_ && config_.ext_processor && config_.ext_processor->enable &&
        !config_.ext_processor->rules.empty()) {
        ext_proc_service_ =
            std::make_unique<engine::pipeline::extproc::FrameEventsExtProcService>(producer_);
        if (!ext_proc_service_->register_handler(handler_id_, pipeline_id_,
                                                 *config_.ext_processor) ||
            !ext_proc_service_->start()) {
            LOG_W("FrameEventsProbeHandler: failed to initialize ext-proc service for handler='{}'",
                  handler_id_);
            ext_proc_service_.reset();
        }
    } else if (config_.ext_processor && config_.ext_processor->enable && !producer_) {
        LOG_W(
            "FrameEventsProbeHandler: ext-proc configured for handler='{}' but producer is not "
            "wired",
            handler_id_);
    }

    for (int index = 0; index < static_cast<int>(config.sources.cameras.size()); ++index) {
        source_id_to_name_[index] = config.sources.cameras[index].id;
    }

    LOG_I(
        "FrameEventsProbeHandler: configured channel='{}' heartbeat_ms={} min_gap_ms={} "
        "label_vote_window_frames={} emit_on_motion_change={} emit_empty_frames={} filters={}",
        broker_channel_, config_.heartbeat_interval_ms, config_.min_emit_gap_ms,
        config_.label_vote_window_frames, config_.emit_on_motion_change, config_.emit_empty_frames,
        label_filter_.size());
}

GstPadProbeReturn FrameEventsProbeHandler::on_buffer(GstPad* /*pad*/, GstPadProbeInfo* info,
                                                     gpointer user_data) {
    auto* self = static_cast<FrameEventsProbeHandler*>(user_data);
    GstBuffer* buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if (!self || !buffer) {
        return GST_PAD_PROBE_OK;
    }

    NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buffer);
    if (!batch_meta) {
        return GST_PAD_PROBE_OK;
    }

    GstMapInfo map_info;
    if (!gst_buffer_map(buffer, &map_info, GST_MAP_READ)) {
        return GST_PAD_PROBE_OK;
    }
    auto* batch_surface = reinterpret_cast<NvBufSurface*>(map_info.data);

    for (NvDsMetaList* frame_iter = batch_meta->frame_meta_list; frame_iter;
         frame_iter = frame_iter->next) {
        auto* frame_meta = static_cast<NvDsFrameMeta*>(frame_iter->data);
        if (!frame_meta) {
            continue;
        }

        const int source_id = static_cast<int>(frame_meta->source_id);
        const std::string source_name = self->resolve_source_name(source_id);
        if (self->ext_proc_service_) {
            self->ext_proc_service_->apply_cached_display_text(self->handler_id_, source_id,
                                                               frame_meta);
        }
        const int64_t emitted_at_ms = now_epoch_ms();
        // Export wall-clock epoch milliseconds so downstream can compare frame_ts_ms
        // directly with emitted_at_ms and use frame_key across services without
        // interpreting DeepStream's pipeline-relative buf_pts domain.
        const int64_t frame_ts_ms = emitted_at_ms;
        const uint64_t frame_num = static_cast<uint64_t>(frame_meta->frame_num);
        const std::string frame_key =
            make_frame_key(self->pipeline_id_, source_name, frame_num, frame_ts_ms);

        std::vector<FrameEventObject> objects;
        self->collect_frame_objects(frame_meta, source_name, frame_key, objects);

        std::vector<std::string> emit_reason;
        if (!self->should_emit_message(source_id, objects, emitted_at_ms, emit_reason)) {
            continue;
        }

        engine::pipeline::evidence::FrameCaptureMetadata capture_meta;
        capture_meta.pipeline_id = self->pipeline_id_;
        capture_meta.source_id = source_id;
        capture_meta.source_name = source_name;
        capture_meta.frame_num = frame_num;
        capture_meta.frame_ts_ms = frame_ts_ms;
        capture_meta.emitted_at_ms = emitted_at_ms;
        capture_meta.frame_key = frame_key;
        capture_meta.overview_ref = build_overview_ref(capture_meta);
        if (batch_surface && frame_meta->batch_id < batch_surface->numFilled) {
            capture_meta.width =
                static_cast<int>(batch_surface->surfaceList[frame_meta->batch_id].width);
            capture_meta.height =
                static_cast<int>(batch_surface->surfaceList[frame_meta->batch_id].height);
        }

        for (auto& object : objects) {
            object.crop_ref = build_crop_ref(capture_meta, object);
        }

        bool cached_frame = false;
        if (self->cache_ && batch_surface) {
            // Cache only emitted frames so downstream evidence requests can target the
            // exact semantic frame_key that triggered business logic.
            std::vector<engine::pipeline::evidence::FrameObjectSnapshot> cached_objects;
            cached_objects.reserve(objects.size());
            for (const auto& object : objects) {
                engine::pipeline::evidence::FrameObjectSnapshot snapshot;
                snapshot.object_key = object.object_key;
                snapshot.instance_key = object.instance_key;
                snapshot.crop_ref = object.crop_ref;
                snapshot.object_id = object.object_id;
                snapshot.tracker_id = object.tracker_id;
                snapshot.class_id = object.class_id;
                snapshot.object_type = object.object_type;
                snapshot.confidence = object.confidence;
                snapshot.left = object.left;
                snapshot.top = object.top;
                snapshot.width = object.width;
                snapshot.height = object.height;
                snapshot.parent_object_key = object.parent_object_key;
                snapshot.parent_instance_key = object.parent_instance_key;
                snapshot.parent_object_id = object.parent_object_id;
                cached_objects.push_back(std::move(snapshot));
            }

            cached_frame = self->cache_->store_frame(capture_meta, cached_objects, batch_surface,
                                                     static_cast<int>(frame_meta->batch_id));
            if (!cached_frame) {
                LOG_W(
                    "FrameEventsProbeHandler: failed to cache emitted frame_key='{}' before "
                    "publish",
                    capture_meta.frame_key);
            }
        }

        self->publish_frame_message(capture_meta, emit_reason, objects);

        self->dispatch_ext_proc_for_frame(frame_meta, batch_surface, capture_meta, objects);
    }

    gst_buffer_unmap(buffer, &map_info);
    return GST_PAD_PROBE_OK;
}

void FrameEventsProbeHandler::collect_frame_objects(
    NvDsFrameMeta* frame_meta, const std::string& source_name, const std::string& frame_key,
    std::vector<FrameEventObject>& out_objects) const {
    out_objects.clear();

    for (NvDsMetaList* object_iter = frame_meta->obj_meta_list; object_iter;
         object_iter = object_iter->next) {
        auto* object_meta = static_cast<NvDsObjectMeta*>(object_iter->data);
        if (!object_meta || object_meta->object_id == kUntrackedObjectId) {
            continue;
        }

        const std::string label = object_meta->obj_label ? object_meta->obj_label : "";
        if (!label_filter_.empty() &&
            std::find(label_filter_.begin(), label_filter_.end(), label) == label_filter_.end()) {
            continue;
        }

        FrameEventObject object;
        object.object_id = static_cast<uint64_t>(object_meta->object_id);
        object.tracker_id = static_cast<uint64_t>(object_meta->object_id);
        object.class_id = static_cast<int>(object_meta->class_id);
        object.object_type = label;
        object.confidence = object_meta->confidence;
        object.labels = collect_sgie_labels(object_meta);
        object.left = object_meta->rect_params.left;
        object.top = object_meta->rect_params.top;
        object.width = object_meta->rect_params.width;
        object.height = object_meta->rect_params.height;
        object.object_key = make_object_key(pipeline_id_, source_name, object.object_id);
        object.instance_key = frame_key + ":" + std::to_string(object.object_id);

        if (object_meta->parent && object_meta->parent->object_id != kUntrackedObjectId) {
            object.parent_object_id = static_cast<int64_t>(object_meta->parent->object_id);
            object.parent_object_key = make_object_key(
                pipeline_id_, source_name, static_cast<uint64_t>(object_meta->parent->object_id));
            object.parent_instance_key =
                frame_key + ":" +
                std::to_string(static_cast<uint64_t>(object_meta->parent->object_id));
        }

        out_objects.push_back(std::move(object));
    }
}

bool FrameEventsProbeHandler::should_emit_message(int source_id,
                                                  std::vector<FrameEventObject>& objects,
                                                  int64_t emitted_at_ms,
                                                  std::vector<std::string>& out_reasons) {
    auto& state = emit_state_[source_id];
    out_reasons.clear();

    if (objects.empty()) {
        // Forget prior detections so the next non-empty frame can become a clean first_frame.
        reset_source_state(source_id);
        return false;
    }

    apply_label_majority_vote(state, objects);

    const std::string signature = build_signature(objects);
    const int64_t elapsed_ms = state.last_emit_ms > 0 ? emitted_at_ms - state.last_emit_ms : 0;

    if (config_.emit_on_first_frame && !state.had_detection) {
        out_reasons.push_back("first_frame");
    }

    if (config_.emit_on_object_set_change && signature != state.last_object_signature) {
        out_reasons.push_back("object_set_change");
    }

    bool has_label_change = false;
    bool has_parent_change = false;
    bool has_motion_change = false;
    for (const auto& object : objects) {
        auto previous_it = state.objects.find(object.object_id);
        if (previous_it == state.objects.end()) {
            continue;
        }

        const auto& previous = previous_it->second;
        const std::string& current_label_signature = object.label_signature;
        if (config_.emit_on_label_change &&
            (previous.object_type != object.object_type || previous.class_id != object.class_id ||
             previous.label_signature != current_label_signature)) {
            has_label_change = true;
        }
        if (config_.emit_on_parent_change && previous.parent_object_id != object.parent_object_id) {
            has_parent_change = true;
        }
        if (config_.emit_on_motion_change) {
            if (compute_iou(previous, object) < config_.motion_iou_threshold) {
                has_motion_change = true;
            } else {
                // IoU alone misses small translations of similarly sized boxes, so we also
                // compare center displacement relative to the previous bbox diagonal.
                const double previous_center_x = previous.left + (previous.width / 2.0);
                const double previous_center_y = previous.top + (previous.height / 2.0);
                const double current_center_x = object.left + (object.width / 2.0);
                const double current_center_y = object.top + (object.height / 2.0);
                const double shift = std::hypot(current_center_x - previous_center_x,
                                                current_center_y - previous_center_y);
                const double diag = std::hypot(previous.width, previous.height);
                if (diag > 0.0 && (shift / diag) >= config_.center_shift_ratio_threshold) {
                    has_motion_change = true;
                }
            }
        }
    }

    if (has_label_change) {
        out_reasons.push_back("label_change");
    }
    if (has_parent_change) {
        out_reasons.push_back("parent_change");
    }
    if (config_.emit_on_motion_change && has_motion_change) {
        out_reasons.push_back("motion_change");
    }
    if (out_reasons.empty() && elapsed_ms >= config_.heartbeat_interval_ms) {
        out_reasons.push_back("heartbeat");
    }

    // Once we have a valid reason, min_emit_gap_ms acts as the final burst guard.
    if (state.last_emit_ms > 0 && elapsed_ms < config_.min_emit_gap_ms) {
        return false;
    }
    if (out_reasons.empty()) {
        return false;
    }

    state.last_emit_ms = emitted_at_ms;
    state.had_detection = true;
    state.last_object_signature = signature;
    state.objects.clear();
    for (const auto& object : objects) {
        LastEmittedObjectState object_state;
        object_state.object_type = object.object_type;
        object_state.label_signature = object.label_signature;
        object_state.class_id = object.class_id;
        object_state.parent_object_id = object.parent_object_id;
        object_state.left = object.left;
        object_state.top = object.top;
        object_state.width = object.width;
        object_state.height = object.height;
        state.objects[object.object_id] = object_state;
    }

    return true;
}

void FrameEventsProbeHandler::publish_frame_message(
    const engine::pipeline::evidence::FrameCaptureMetadata& meta,
    const std::vector<std::string>& emit_reason,
    const std::vector<FrameEventObject>& objects) const {
    if (!producer_ || broker_channel_.empty()) {
        return;
    }

    json message = json::object();
    message["event"] = "frame_events";
    message["pipeline_id"] = meta.pipeline_id;
    message["source_id"] = meta.source_id;
    message["source_name"] = meta.source_name;
    message["frame_num"] = meta.frame_num;
    message["frame_ts_ms"] = meta.frame_ts_ms;
    message["emitted_at_ms"] = meta.emitted_at_ms;
    message["frame_key"] = meta.frame_key;
    message["overview_ref"] = meta.overview_ref;
    message["emit_reason"] = emit_reason;
    message["object_count"] = objects.size();

    json object_list = json::array();
    const int frame_width = source_width_ > 0 ? source_width_ : meta.width;
    const int frame_height = source_height_ > 0 ? source_height_ : meta.height;
    for (const auto& object : objects) {
        json item = json::object();
        item["object_key"] = object.object_key;
        item["instance_key"] = object.instance_key;
        item["object_id"] = object.object_id;
        item["tracker_id"] = object.tracker_id;
        item["class_id"] = object.class_id;
        item["object_type"] = object.object_type;
        item["confidence"] = object.confidence;
        item["labels"] = object.labels;
        item["crop_ref"] = object.crop_ref;
        item["bbox"] = {{"left", object.left},        {"top", object.top},
                        {"width", object.width},      {"height", object.height},
                        {"frame_width", frame_width}, {"frame_height", frame_height}};
        item["parent_object_key"] = object.parent_object_key;
        item["parent_instance_key"] = object.parent_instance_key;
        item["parent_object_id"] = object.parent_object_id;
        object_list.push_back(std::move(item));
    }

    message["objects"] = std::move(object_list);
    producer_->publish_json(broker_channel_, message.dump());
}

void FrameEventsProbeHandler::dispatch_ext_proc_for_frame(
    NvDsFrameMeta* frame_meta, NvBufSurface* batch_surface,
    const engine::pipeline::evidence::FrameCaptureMetadata& meta,
    const std::vector<FrameEventObject>& objects) const {
    if (!ext_proc_service_ || !config_.ext_processor || !config_.ext_processor->enable ||
        !frame_meta || !batch_surface) {
        return;
    }

    std::unordered_map<uint64_t, const FrameEventObject*> emitted_objects;
    emitted_objects.reserve(objects.size());
    for (const auto& object : objects) {
        emitted_objects[object.object_id] = &object;
    }

    for (NvDsMetaList* object_iter = frame_meta->obj_meta_list; object_iter;
         object_iter = object_iter->next) {
        auto* object_meta = static_cast<NvDsObjectMeta*>(object_iter->data);
        if (!object_meta || object_meta->object_id == kUntrackedObjectId) {
            continue;
        }

        const uint64_t object_id = static_cast<uint64_t>(object_meta->object_id);
        const auto emitted_it = emitted_objects.find(object_id);
        if (emitted_it == emitted_objects.end()) {
            continue;
        }

        const auto& object = *emitted_it->second;
        ext_proc_service_->process_object(
            handler_id_, meta.source_id, meta.source_name, meta.frame_key, meta.frame_ts_ms,
            meta.overview_ref, object.crop_ref, object.object_key, object.instance_key,
            object.object_id, object.tracker_id, object.class_id, object.object_type,
            object.confidence, object_meta, frame_meta, batch_surface);
    }
}

std::string FrameEventsProbeHandler::resolve_source_name(int source_id) const {
    const std::string runtime_source_name =
        engine::pipeline::lookup_runtime_source_name(source_root_, source_id);
    if (!runtime_source_name.empty()) {
        return runtime_source_name;
    }

    const auto source_name_it = source_id_to_name_.find(source_id);
    if (source_name_it != source_id_to_name_.end() && !source_name_it->second.empty()) {
        return source_name_it->second;
    }

    return "source_" + std::to_string(source_id);
}

void FrameEventsProbeHandler::reset_source_state(int source_id) {
    auto it = emit_state_.find(source_id);
    if (it == emit_state_.end()) {
        return;
    }

    it->second.had_detection = false;
    it->second.label_votes.clear();
    it->second.objects.clear();
    it->second.last_object_signature.clear();
}

void FrameEventsProbeHandler::apply_label_majority_vote(
    PerSourceEmitState& state, std::vector<FrameEventObject>& objects) const {
    const size_t window_size = static_cast<size_t>(std::max(1, config_.label_vote_window_frames));
    std::unordered_set<uint64_t> seen_object_ids;
    seen_object_ids.reserve(objects.size());

    for (auto& object : objects) {
        seen_object_ids.insert(object.object_id);

        auto& vote_state = state.label_votes[object.object_id];
        LabelVoteSample sample;
        sample.signature = build_label_signature(object.labels);
        sample.labels = object.labels;
        if (!vote_state.provisional_initialized) {
            vote_state.provisional_initialized = true;
            vote_state.provisional_labels = object.labels;
        }
        vote_state.samples.push_back(std::move(sample));
        while (vote_state.samples.size() > window_size) {
            vote_state.samples.pop_front();
        }

        if (window_size == 1 && !vote_state.samples.empty()) {
            const auto& latest = vote_state.samples.back();
            vote_state.committed_signature = latest.signature;
            vote_state.committed_labels = latest.labels;
        } else if (vote_state.samples.size() >= window_size) {
            std::unordered_map<std::string, int> counts;
            std::unordered_map<std::string, size_t> latest_index;
            std::unordered_map<std::string, std::vector<std::string>> latest_labels;
            for (size_t index = 0; index < vote_state.samples.size(); ++index) {
                const auto& observed = vote_state.samples[index];
                counts[observed.signature] += 1;
                latest_index[observed.signature] = index;
                latest_labels[observed.signature] = observed.labels;
            }

            std::string winner_signature = vote_state.committed_signature;
            int winner_count = -1;
            size_t winner_latest_index = 0;
            for (const auto& [signature, count] : counts) {
                const size_t candidate_latest_index = latest_index[signature];
                if (count > winner_count ||
                    (count == winner_count && candidate_latest_index > winner_latest_index)) {
                    winner_signature = signature;
                    winner_count = count;
                    winner_latest_index = candidate_latest_index;
                }
            }

            if (winner_count > static_cast<int>(vote_state.samples.size() / 2U)) {
                vote_state.committed_signature = winner_signature;
                vote_state.committed_labels = latest_labels[winner_signature];
            }
        }

        if (!vote_state.committed_signature.empty() || !vote_state.committed_labels.empty()) {
            object.labels = vote_state.committed_labels;
            object.label_signature = vote_state.committed_signature;
        } else {
            object.labels = vote_state.provisional_labels;
            object.label_signature.clear();
        }
    }

    for (auto vote_it = state.label_votes.begin(); vote_it != state.label_votes.end();) {
        if (seen_object_ids.find(vote_it->first) == seen_object_ids.end()) {
            vote_it = state.label_votes.erase(vote_it);
        } else {
            ++vote_it;
        }
    }
}

double FrameEventsProbeHandler::compute_iou(const LastEmittedObjectState& previous,
                                            const FrameEventObject& current) const {
    const float previous_right = previous.left + previous.width;
    const float previous_bottom = previous.top + previous.height;
    const float current_right = current.left + current.width;
    const float current_bottom = current.top + current.height;

    const float inter_left = std::max(previous.left, current.left);
    const float inter_top = std::max(previous.top, current.top);
    const float inter_right = std::min(previous_right, current_right);
    const float inter_bottom = std::min(previous_bottom, current_bottom);
    const float inter_width = std::max(0.0F, inter_right - inter_left);
    const float inter_height = std::max(0.0F, inter_bottom - inter_top);
    const float inter_area = inter_width * inter_height;

    const float previous_area = previous.width * previous.height;
    const float current_area = current.width * current.height;
    const float union_area = previous_area + current_area - inter_area;
    if (union_area <= 0.0F) {
        return 0.0;
    }
    return static_cast<double>(inter_area / union_area);
}

}  // namespace engine::pipeline::probes