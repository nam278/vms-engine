#include "engine/pipeline/evidence/evidence_request_service.hpp"

#include "engine/core/messaging/imessage_producer.hpp"
#include "engine/core/utils/logger.hpp"
#include "engine/pipeline/evidence/frame_evidence_cache.hpp"

#include <gstnvdsmeta.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace engine::pipeline::evidence {

namespace {

int64_t now_epoch_ms() {
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

std::string json_string(const json& node, const char* key, const std::string& def = "") {
    if (!node.contains(key) || node.at(key).is_null()) {
        return def;
    }
    if (node.at(key).is_string()) {
        return node.at(key).get<std::string>();
    }
    return node.at(key).dump();
}

int64_t json_int64(const json& node, const char* key, int64_t def = 0) {
    if (!node.contains(key) || node.at(key).is_null()) {
        return def;
    }
    try {
        if (node.at(key).is_number_integer()) {
            return node.at(key).get<int64_t>();
        }
        if (node.at(key).is_number_unsigned()) {
            return static_cast<int64_t>(node.at(key).get<uint64_t>());
        }
        if (node.at(key).is_string()) {
            return std::stoll(node.at(key).get<std::string>());
        }
    } catch (...) {
    }
    return def;
}

double json_double(const json& node, const char* key, double def = 0.0) {
    if (!node.contains(key) || node.at(key).is_null()) {
        return def;
    }
    try {
        if (node.at(key).is_number()) {
            return node.at(key).get<double>();
        }
        if (node.at(key).is_string()) {
            return std::stod(node.at(key).get<std::string>());
        }
    } catch (...) {
    }
    return def;
}

json json_from_string_or_value(const json& value) {
    if (value.is_string()) {
        try {
            return json::parse(value.get<std::string>());
        } catch (...) {
            return json();
        }
    }
    return value;
}

std::vector<std::string> parse_string_list(const json& node, const char* key) {
    std::vector<std::string> result;
    if (!node.contains(key)) {
        return result;
    }

    json list_value = json_from_string_or_value(node.at(key));
    if (list_value.is_array()) {
        for (const auto& item : list_value) {
            if (item.is_string()) {
                result.push_back(item.get<std::string>());
            } else {
                result.push_back(item.dump());
            }
        }
    } else if (list_value.is_string()) {
        result.push_back(list_value.get<std::string>());
    }
    return result;
}

std::string make_default_request_id() {
    std::ostringstream oss;
    oss << "req-" << now_epoch_ms();
    return oss.str();
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

std::string build_default_crop_ref(const FrameCaptureMetadata& meta, int crop_index) {
    const std::string safe_pipeline = sanitize_ref_component(meta.pipeline_id);
    const std::string safe_source_name = sanitize_ref_component(meta.source_name);
    return safe_pipeline + "_" + safe_source_name + "_" + std::to_string(meta.frame_num) + "_" +
           std::to_string(meta.frame_ts_ms) + "_crop_req_" + std::to_string(crop_index) + ".jpg";
}

bool wants_type(const std::vector<std::string>& evidence_types, const std::string& type) {
    if (evidence_types.empty()) {
        return type == "overview";
    }
    return std::find(evidence_types.begin(), evidence_types.end(), type) != evidence_types.end();
}

const FrameObjectSnapshot* find_cached_object(const CachedFrameEntry& entry,
                                              const EvidenceRequestObject& request_object) {
    for (const auto& object : entry.objects) {
        if (!request_object.object_key.empty() && object.object_key == request_object.object_key) {
            return &object;
        }
        if (!request_object.instance_key.empty() &&
            object.instance_key == request_object.instance_key) {
            return &object;
        }
        if (request_object.object_id >= 0 &&
            object.object_id == static_cast<uint64_t>(request_object.object_id)) {
            return &object;
        }
    }
    return nullptr;
}

}  // namespace

EvidenceRequestService::EvidenceRequestService(const engine::core::config::EvidenceConfig& config,
                                               engine::core::messaging::IMessageProducer* producer,
                                               FrameEvidenceCache* cache)
    : config_(config), producer_(producer), cache_(cache) {}

EvidenceRequestService::~EvidenceRequestService() {
    stop();
}

bool EvidenceRequestService::start() {
    if (worker_thread_.joinable()) {
        return true;
    }

    stop_.store(false);
    worker_thread_ = std::thread(&EvidenceRequestService::worker_loop, this);
    return true;
}

void EvidenceRequestService::stop() {
    stop_.store(true);
    cv_.notify_all();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

bool EvidenceRequestService::enqueue_request(const std::string& payload) {
    EvidenceRequestJob job;
    if (!parse_request_payload(payload, job)) {
        LOG_W("EvidenceRequestService: invalid evidence_request payload");
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        jobs_.push(std::move(job));
    }
    cv_.notify_one();
    return true;
}

bool EvidenceRequestService::parse_request_payload(const std::string& payload,
                                                   EvidenceRequestJob& out_job) const {
    json node;
    try {
        node = json::parse(payload);
    } catch (const std::exception& ex) {
        LOG_W("EvidenceRequestService: JSON parse error: {}", ex.what());
        return false;
    }

    out_job.raw_payload = payload;
    out_job.schema_version = json_string(node, "schema_version", "1.0");
    out_job.request_id = json_string(node, "request_id", make_default_request_id());
    out_job.pipeline_id = json_string(node, "pipeline_id");
    out_job.source_name = json_string(node, "source_name");
    if (out_job.source_name.empty()) {
        out_job.source_name = json_string(node, "camera_id");
    }
    out_job.source_id = static_cast<int>(json_int64(node, "source_id", -1));
    out_job.frame_key = json_string(node, "frame_key");
    out_job.frame_ts_ms = json_int64(node, "frame_ts_ms", 0);
    out_job.overview_ref = json_string(node, "overview_ref");
    out_job.event_id = json_string(node, "event_id");
    out_job.timeline_id = json_string(node, "timeline_id");
    out_job.evidence_types = parse_string_list(node, "evidence_types");

    if (out_job.pipeline_id.empty() || out_job.source_name.empty() || out_job.source_id < 0 ||
        out_job.frame_key.empty()) {
        return false;
    }

    if (node.contains("objects")) {
        json objects_node = json_from_string_or_value(node.at("objects"));
        if (objects_node.is_array()) {
            for (const auto& object_node : objects_node) {
                if (!object_node.is_object()) {
                    continue;
                }

                EvidenceRequestObject object;
                object.object_key = json_string(object_node, "object_key");
                object.instance_key = json_string(object_node, "instance_key");
                object.crop_ref = json_string(object_node, "crop_ref");
                object.object_id = json_int64(object_node, "object_id", -1);

                // Callers can identify the target object precisely or fall back to a bbox-only
                // request.
                json bbox_node = object_node.contains("bbox")
                                     ? json_from_string_or_value(object_node.at("bbox"))
                                     : json();
                if (bbox_node.is_object()) {
                    object.left = static_cast<float>(json_double(bbox_node, "left", 0.0));
                    object.top = static_cast<float>(json_double(bbox_node, "top", 0.0));
                    object.width = static_cast<float>(json_double(bbox_node, "width", 0.0));
                    object.height = static_cast<float>(json_double(bbox_node, "height", 0.0));
                    object.has_bbox = object.width > 0.0F && object.height > 0.0F;
                }

                out_job.objects.push_back(std::move(object));
            }
        }
    }

    return true;
}

void EvidenceRequestService::worker_loop() {
    enc_ctx_ = nvds_obj_enc_create_context(0);
    if (!enc_ctx_) {
        LOG_E("EvidenceRequestService: failed to create NvDsObjEnc context");
        return;
    }

    while (true) {
        EvidenceRequestJob job;
        {
            std::unique_lock<std::mutex> lk(queue_mutex_);
            cv_.wait(lk, [this] { return stop_.load() || !jobs_.empty(); });
            if (stop_.load() && jobs_.empty()) {
                break;
            }
            job = std::move(jobs_.front());
            jobs_.pop();
        }

        process_job(job);
    }

    nvds_obj_enc_destroy_context(enc_ctx_);
    enc_ctx_ = nullptr;
}

void EvidenceRequestService::process_job(const EvidenceRequestJob& job) {
    if (!cache_ || !producer_) {
        return;
    }

    auto entry = cache_->resolve(job.pipeline_id, job.source_name, job.source_id, job.frame_key,
                                 job.frame_ts_ms);
    if (!entry) {
        publish_ready(job, "not_found", "", {}, "frame_not_in_cache");
        return;
    }

    std::string overview_ref;
    std::vector<std::string> crop_refs;
    std::string failure_reason;

    if (wants_type(job.evidence_types, "overview")) {
        if (!encode_overview(*entry, job, overview_ref, failure_reason)) {
            publish_ready(job, "error", "", {},
                          failure_reason.empty() ? "overview_encode_failed" : failure_reason);
            return;
        }
    }

    if (wants_type(job.evidence_types, "crop")) {
        if (!encode_crops(*entry, job, crop_refs, failure_reason)) {
            publish_ready(job, "error", overview_ref, {}, failure_reason);
            return;
        }
    }

    publish_ready(job, "ok", overview_ref, crop_refs, "");
}

bool EvidenceRequestService::encode_overview(const CachedFrameEntry& entry,
                                             const EvidenceRequestJob& job, std::string& out_ref,
                                             std::string& failure_reason) {
    if (!enc_ctx_ || !entry.surface) {
        failure_reason = "encoder_not_ready";
        return false;
    }

    const std::string output_ref =
        !job.overview_ref.empty() ? job.overview_ref : entry.meta.overview_ref;
    std::string file_path;
    if (!resolve_output_path(output_ref, file_path, failure_reason)) {
        return false;
    }

    NvDsFrameMeta frame_meta{};
    frame_meta.batch_id = 0;
    frame_meta.source_id = static_cast<guint>(entry.meta.source_id);
    frame_meta.frame_num = static_cast<gint>(entry.meta.frame_num);
    frame_meta.buf_pts = static_cast<guint64>(entry.meta.frame_ts_ms) * GST_MSECOND;

    NvDsObjectMeta object_meta{};
    object_meta.rect_params.left = 0.0F;
    object_meta.rect_params.top = 0.0F;
    object_meta.rect_params.width = static_cast<float>(entry.meta.width);
    object_meta.rect_params.height = static_cast<float>(entry.meta.height);

    NvDsObjEncUsrArgs args{};
    args.saveImg = TRUE;
    args.attachUsrMeta = FALSE;
    args.scaleImg = FALSE;
    args.quality = config_.overview_jpeg_quality;
    args.isFrame = TRUE;
    std::snprintf(args.fileNameImg, sizeof(args.fileNameImg), "%s", file_path.c_str());

    if (!nvds_obj_enc_process(enc_ctx_, &args, entry.surface, &object_meta, &frame_meta)) {
        return false;
    }
    nvds_obj_enc_finish(enc_ctx_);
    out_ref = file_path;
    return true;
}

bool EvidenceRequestService::encode_crops(const CachedFrameEntry& entry,
                                          const EvidenceRequestJob& job,
                                          std::vector<std::string>& out_refs,
                                          std::string& failure_reason) {
    if (!enc_ctx_ || !entry.surface) {
        failure_reason = "encoder_not_ready";
        return false;
    }

    std::vector<EvidenceRequestObject> request_objects = job.objects;
    if (request_objects.empty()) {
        // No explicit object filter means "materialize crops for everything visible on that frame".
        for (const auto& object : entry.objects) {
            EvidenceRequestObject request_object;
            request_object.object_key = object.object_key;
            request_object.instance_key = object.instance_key;
            request_object.crop_ref = object.crop_ref;
            request_object.object_id = static_cast<int64_t>(object.object_id);
            request_object.has_bbox = true;
            request_object.left = object.left;
            request_object.top = object.top;
            request_object.width = object.width;
            request_object.height = object.height;
            request_objects.push_back(std::move(request_object));
        }
    }

    NvDsFrameMeta frame_meta{};
    frame_meta.batch_id = 0;
    frame_meta.source_id = static_cast<guint>(entry.meta.source_id);
    frame_meta.frame_num = static_cast<gint>(entry.meta.frame_num);
    frame_meta.buf_pts = static_cast<guint64>(entry.meta.frame_ts_ms) * GST_MSECOND;

    int crop_index = 0;
    for (const auto& request_object : request_objects) {
        float left = request_object.left;
        float top = request_object.top;
        float width = request_object.width;
        float height = request_object.height;
        int class_id = 0;
        uint64_t object_id = 0;

        const FrameObjectSnapshot* cached_object = find_cached_object(entry, request_object);
        if (cached_object) {
            left = cached_object->left;
            top = cached_object->top;
            width = cached_object->width;
            height = cached_object->height;
            class_id = cached_object->class_id;
            object_id = cached_object->object_id;
        } else if (!request_object.has_bbox) {
            failure_reason = "requested_object_not_found";
            return false;
        }

        std::string output_ref = request_object.crop_ref;
        if (output_ref.empty() && cached_object && !cached_object->crop_ref.empty()) {
            output_ref = cached_object->crop_ref;
        }
        if (output_ref.empty()) {
            output_ref = build_default_crop_ref(entry.meta, crop_index);
        }

        std::string file_path;
        if (!resolve_output_path(output_ref, file_path, failure_reason)) {
            return false;
        }
        ++crop_index;

        NvDsObjectMeta object_meta{};
        object_meta.object_id = static_cast<guint64>(object_id);
        object_meta.class_id = class_id;
        object_meta.rect_params.left = left;
        object_meta.rect_params.top = top;
        object_meta.rect_params.width = width;
        object_meta.rect_params.height = height;

        NvDsObjEncUsrArgs args{};
        args.saveImg = TRUE;
        args.attachUsrMeta = FALSE;
        args.scaleImg = FALSE;
        args.quality = config_.overview_jpeg_quality;
        args.isFrame = FALSE;
        std::snprintf(args.fileNameImg, sizeof(args.fileNameImg), "%s", file_path.c_str());

        if (!nvds_obj_enc_process(enc_ctx_, &args, entry.surface, &object_meta, &frame_meta)) {
            failure_reason = "crop_encode_failed";
            return false;
        }

        out_refs.push_back(file_path);
    }

    if (!out_refs.empty()) {
        nvds_obj_enc_finish(enc_ctx_);
    }
    return true;
}

bool EvidenceRequestService::resolve_output_path(const std::string& ref, std::string& out_path,
                                                 std::string& failure_reason) const {
    if (ref.empty()) {
        failure_reason = "missing_output_ref";
        return false;
    }

    const fs::path ref_path(ref);
    if (ref_path.is_absolute()) {
        failure_reason = "absolute_output_ref_not_allowed";
        return false;
    }

    for (const auto& part : ref_path) {
        if (part == "..") {
            failure_reason = "invalid_output_ref";
            return false;
        }
    }

    const fs::path base_dir = config_.save_dir.empty() ? fs::path("/opt/vms_engine/dev/rec/frames")
                                                       : fs::path(config_.save_dir);
    const fs::path full_path = (base_dir / ref_path).lexically_normal();

    std::error_code ec;
    fs::create_directories(full_path.parent_path(), ec);
    if (ec) {
        failure_reason = "create_output_dir_failed";
        return false;
    }

    out_path = full_path.string();
    return true;
}

void EvidenceRequestService::publish_ready(const EvidenceRequestJob& job, const std::string& status,
                                           const std::string& overview_ref,
                                           const std::vector<std::string>& crop_refs,
                                           const std::string& failure_reason) const {
    if (!producer_ || config_.ready_channel.empty()) {
        return;
    }

    json ready = json::object();
    // This is a completion event for media-side consumers, not a synchronous response to Python.
    ready["event"] = "evidence_ready";
    ready["schema_version"] = job.schema_version;
    ready["request_id"] = job.request_id;
    ready["pipeline_id"] = job.pipeline_id;
    ready["source_name"] = job.source_name;
    ready["source_id"] = job.source_id;
    ready["frame_key"] = job.frame_key;
    ready["frame_ts_ms"] = job.frame_ts_ms;
    ready["status"] = status;
    ready["event_id"] = job.event_id;
    ready["timeline_id"] = job.timeline_id;
    ready["generated_at_ms"] = now_epoch_ms();
    if (!overview_ref.empty()) {
        ready["overview_ref"] = overview_ref;
    }
    if (!crop_refs.empty()) {
        ready["crop_refs"] = crop_refs;
    }
    if (!failure_reason.empty()) {
        ready["failure_reason"] = failure_reason;
    }

    producer_->publish_json(config_.ready_channel, ready.dump());
}

}  // namespace engine::pipeline::evidence