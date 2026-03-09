#include "engine/pipeline/extproc/frame_events_ext_proc_service.hpp"

#include "engine/core/messaging/imessage_producer.hpp"
#include "engine/core/utils/logger.hpp"
#include "engine/pipeline/evidence/frame_evidence_cache.hpp"
#include "engine/pipeline/evidence/frame_image_materializer.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <nvds_obj_encode.h>

#include <algorithm>
#include <chrono>
#include <sstream>

using json = nlohmann::json;

namespace engine::pipeline::extproc {

namespace {

size_t curl_write_callback(void* contents, size_t size, size_t nmemb, std::string* out) noexcept {
    out->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

int64_t monotonic_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

int64_t epoch_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string json_get_by_path(const json& node, const std::string& path) {
    if (path.empty()) {
        return "";
    }

    try {
        std::string pointer_path;
        pointer_path.reserve(path.size() + 1);
        pointer_path.push_back('/');
        for (const char ch : path) {
            pointer_path.push_back(ch == '.' ? '/' : ch);
        }

        const auto& value = node.at(json::json_pointer(pointer_path));
        return value.is_string() ? value.get<std::string>() : value.dump();
    } catch (...) {
        return "";
    }
}

const engine::core::config::FrameEventsExtProcRule* find_rule(
    const engine::core::config::FrameEventsExtProcConfig& config, const std::string& label) {
    for (const auto& rule : config.rules) {
        if (rule.label == label) {
            return &rule;
        }
    }
    return nullptr;
}

std::string build_throttle_key(const FrameEventsExtProcJob& job) {
    std::ostringstream oss;
    oss << job.pipeline_id << ':' << job.source_id << ':' << job.object_id << ':'
        << job.object_type;
    return oss.str();
}

}  // namespace

FrameEventsExtProcService::FrameEventsExtProcService(
    engine::core::messaging::IMessageProducer* producer,
    engine::pipeline::evidence::FrameEvidenceCache* cache)
    : producer_(producer), cache_(cache) {}

FrameEventsExtProcService::~FrameEventsExtProcService() {
    stop();
}

bool FrameEventsExtProcService::register_handler(
    const std::string& handler_id, const std::string& pipeline_id,
    const engine::core::config::FrameEventsExtProcConfig& config) {
    if (handler_id.empty() || !config.enable || config.publish_channel.empty() ||
        config.rules.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lk(mutex_);
    handlers_[handler_id] = RegisteredHandler{pipeline_id, config};
    queue_capacity_ =
        std::max(queue_capacity_, static_cast<size_t>(std::max(1, config.queue_capacity)));
    worker_count_ =
        std::max(worker_count_, static_cast<size_t>(std::max(1, config.worker_threads)));

    LOG_I(
        "FrameEventsExtProcService: registered handler='{}' channel='{}' workers={} queue={} "
        "min_interval_sec={} jpeg_quality={} rules={}",
        handler_id, config.publish_channel, std::max(1, config.worker_threads),
        std::max(1, config.queue_capacity), config.min_interval_sec, config.jpeg_quality,
        config.rules.size());
    return true;
}

bool FrameEventsExtProcService::start() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (started_) {
        return true;
    }
    if (!producer_ || !cache_ || handlers_.empty()) {
        return false;
    }

    stop_.store(false);
    workers_.clear();
    workers_.reserve(worker_count_);
    for (size_t index = 0; index < worker_count_; ++index) {
        workers_.emplace_back(&FrameEventsExtProcService::worker_loop, this);
    }
    started_ = true;
    return true;
}

void FrameEventsExtProcService::stop() {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!started_) {
            return;
        }
        started_ = false;
        stop_.store(true);
    }

    cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();

    std::lock_guard<std::mutex> lk(mutex_);
    jobs_.clear();
}

bool FrameEventsExtProcService::enqueue(const FrameEventsExtProcJob& job) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!started_ || stop_.load()) {
        return false;
    }

    if (jobs_.size() >= queue_capacity_) {
        LOG_W(
            "FrameEventsExtProcService: dropping ext-proc job handler='{}' frame_key='{}' "
            "object_key='{}' because queue is full ({})",
            job.handler_id, job.frame_key, job.object_key, queue_capacity_);
        return false;
    }

    jobs_.push_back(job);
    cv_.notify_one();
    return true;
}

bool FrameEventsExtProcService::is_running() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return started_ && !stop_.load();
}

void FrameEventsExtProcService::worker_loop() {
    NvDsObjEncCtxHandle enc_ctx = nvds_obj_enc_create_context(0);
    if (!enc_ctx) {
        LOG_E("FrameEventsExtProcService: failed to create NvDsObjEnc context for worker");
        return;
    }

    while (true) {
        FrameEventsExtProcJob job;
        {
            std::unique_lock<std::mutex> lk(mutex_);
            cv_.wait(lk, [this] { return stop_.load() || !jobs_.empty(); });
            if (stop_.load() && jobs_.empty()) {
                break;
            }

            job = std::move(jobs_.front());
            jobs_.pop_front();
        }

        process_job(enc_ctx, job);
    }

    nvds_obj_enc_destroy_context(enc_ctx);
}

void FrameEventsExtProcService::process_job(void* enc_ctx_void, const FrameEventsExtProcJob& job) {
    auto* enc_ctx = static_cast<NvDsObjEncCtxHandle>(enc_ctx_void);

    RegisteredHandler handler;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        const auto handler_it = handlers_.find(job.handler_id);
        if (handler_it == handlers_.end()) {
            return;
        }
        handler = handler_it->second;

        const std::string throttle_key = build_throttle_key(job);
        const int64_t now_ms = monotonic_ms();
        auto throttle_it = throttle_state_ms_.find(throttle_key);
        if (throttle_it != throttle_state_ms_.end() &&
            (now_ms - throttle_it->second) <
                static_cast<int64_t>(handler.config.min_interval_sec) * 1000) {
            return;
        }
        throttle_state_ms_[throttle_key] = now_ms;
    }

    const auto* rule = find_rule(handler.config, job.object_type);
    if (!rule || !cache_ || !producer_) {
        return;
    }

    const auto entry = cache_->resolve(job.pipeline_id, job.source_name, job.source_id,
                                       job.frame_key, job.frame_ts_ms);
    if (!entry) {
        LOG_D(
            "FrameEventsExtProcService: skip ext-proc handler='{}' frame_key='{}' because "
            "frame is not in cache",
            job.handler_id, job.frame_key);
        return;
    }

    float left = job.left;
    float top = job.top;
    float width = job.width;
    float height = job.height;
    int class_id = job.class_id;
    uint64_t object_id = job.object_id;
    std::string crop_ref = job.crop_ref;

    for (const auto& object : entry->objects) {
        if ((!job.object_key.empty() && object.object_key == job.object_key) ||
            (!job.instance_key.empty() && object.instance_key == job.instance_key) ||
            object.object_id == job.object_id) {
            left = object.left;
            top = object.top;
            width = object.width;
            height = object.height;
            class_id = object.class_id;
            object_id = object.object_id;
            if (crop_ref.empty()) {
                crop_ref = object.crop_ref;
            }
            break;
        }
    }

    std::vector<unsigned char> jpeg_bytes;
    std::string failure_reason;
    if (!engine::pipeline::evidence::FrameImageMaterializer::encode_crop_to_bytes(
            enc_ctx, *entry, left, top, width, height, class_id, object_id,
            handler.config.jpeg_quality, jpeg_bytes, failure_reason)) {
        LOG_W(
            "FrameEventsExtProcService: failed to materialize crop bytes handler='{}' "
            "frame_key='{}' object_key='{}' reason='{}'",
            job.handler_id, job.frame_key, job.object_key, failure_reason);
        return;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG_W("FrameEventsExtProcService: curl_easy_init failed for handler='{}'", job.handler_id);
        return;
    }

    std::string url = rule->endpoint;
    if (!rule->params.empty()) {
        url.push_back('?');
        bool first = true;
        for (const auto& [key, value] : rule->params) {
            if (!first) {
                url.push_back('&');
            }
            char* escaped_key = curl_easy_escape(curl, key.c_str(), static_cast<int>(key.size()));
            char* escaped_value =
                curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size()));
            url += escaped_key ? escaped_key : "";
            url.push_back('=');
            url += escaped_value ? escaped_value : "";
            if (escaped_key) {
                curl_free(escaped_key);
            }
            if (escaped_value) {
                curl_free(escaped_value);
            }
            first = false;
        }
    }

    curl_mime* mime = curl_mime_init(curl);
    curl_mimepart* part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");
    curl_mime_filename(part, "image.jpg");
    curl_mime_type(part, "image/jpeg");
    curl_mime_data(part, reinterpret_cast<const char*>(jpeg_bytes.data()), jpeg_bytes.size());

    std::string response_body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
                     static_cast<long>(handler.config.connect_timeout_ms));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,
                     static_cast<long>(handler.config.request_timeout_ms));
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "FrameEventsExtProcService/1.0");

    const CURLcode curl_code = curl_easy_perform(curl);
    curl_mime_free(mime);
    curl_easy_cleanup(curl);

    if (curl_code != CURLE_OK) {
        LOG_W("FrameEventsExtProcService: HTTP error handler='{}' object_key='{}': {}",
              job.handler_id, job.object_key, curl_easy_strerror(curl_code));
        return;
    }

    FrameEventsExtProcResult result;
    try {
        const auto parsed = json::parse(response_body);
        result.result = json_get_by_path(parsed, rule->result_path);
        result.display = json_get_by_path(parsed, rule->display_path);
    } catch (const std::exception& ex) {
        LOG_W("FrameEventsExtProcService: JSON parse error handler='{}' object_key='{}': {}",
              job.handler_id, job.object_key, ex.what());
        return;
    }

    if (result.result.empty() && !handler.config.emit_empty_result) {
        return;
    }
    if (result.result.empty()) {
        result.status = "empty_result";
    }

    json message = json::object();
    message["event"] = "ext_proc";
    message["schema_version"] = job.schema_version;
    message["status"] = result.status;
    message["pid"] = job.pipeline_id;
    message["pipeline_id"] = job.pipeline_id;
    message["sid"] = job.source_id;
    message["source_id"] = job.source_id;
    message["sname"] = job.source_name;
    message["source_name"] = job.source_name;
    message["frame_key"] = job.frame_key;
    message["frame_ts_ms"] = job.frame_ts_ms;
    message["instance_key"] = job.instance_key;
    message["oid"] = job.object_id;
    message["tracker_id"] = job.tracker_id;
    message["object_key"] = job.object_key;
    message["class"] = job.object_type;
    message["class_id"] = job.class_id;
    message["conf"] = job.confidence;
    message["labels"] =
        result.display.empty() ? result.result : result.result + "|" + result.display;
    message["result"] = result.result;
    message["display"] = result.display;
    message["crop_ref"] = crop_ref;
    if (handler.config.include_overview_ref) {
        message["overview_ref"] = job.overview_ref;
    }
    message["event_ts"] = std::to_string(epoch_ms());

    producer_->publish_json(handler.config.publish_channel, message.dump());
}

}  // namespace engine::pipeline::extproc