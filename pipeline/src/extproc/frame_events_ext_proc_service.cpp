#include "engine/pipeline/extproc/frame_events_ext_proc_service.hpp"

#include "engine/core/messaging/imessage_producer.hpp"
#include "engine/core/utils/logger.hpp"

#include <curl/curl.h>
#include <gstnvdsmeta.h>
#include <nvbufsurface.h>
#include <nvds_obj_encode.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

namespace engine::pipeline::extproc {

namespace {

constexpr int64_t kMinimumOsdOverrideRetentionMs = 5000;
constexpr int64_t kDefaultOsdOverrideRetentionMs = 30000;

size_t curl_write_callback(void* contents, size_t size, size_t nmemb, std::string* out) noexcept {
    out->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

int64_t epoch_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

int64_t monotonic_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
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

std::string make_osd_override_key(const std::string& handler_id, int source_id,
                                  uint64_t tracker_id) {
    return handler_id + ":" + std::to_string(source_id) + ":" + std::to_string(tracker_id);
}

void apply_object_display_text(NvDsObjectMeta* obj_meta, const std::string& text) {
    if (!obj_meta || text.empty()) {
        return;
    }

    const char* current_text = obj_meta->text_params.display_text;
    if (current_text && text == current_text) {
        return;
    }

    if (obj_meta->text_params.display_text) {
        g_free(obj_meta->text_params.display_text);
    }

    obj_meta->text_params.display_text = g_strdup(text.c_str());
}

}  // namespace

struct FrameEventsExtProcService::Impl {
    struct RegisteredHandler {
        std::string pipeline_id;
        engine::core::config::FrameEventsExtProcConfig config;
    };

    struct OsdOverrideEntry {
        std::string text;
        std::string object_type;
        int64_t updated_at_ms = 0;
        int64_t last_seen_at_ms = 0;
        int64_t stale_after_ms = kDefaultOsdOverrideRetentionMs;
    };

    engine::core::messaging::IMessageProducer* producer = nullptr;
    std::mutex mutex;
    std::unordered_map<std::string, RegisteredHandler> handlers;
    std::unordered_map<std::string, OsdOverrideEntry> osd_overrides;
    NvDsObjEncCtxHandle obj_enc_ctx = nullptr;
    bool started = false;

    void cleanup_stale_osd_overrides_locked(int64_t now_ms) {
        for (auto it = osd_overrides.begin(); it != osd_overrides.end();) {
            const int64_t last_touch_ms =
                std::max(it->second.updated_at_ms, it->second.last_seen_at_ms);
            if (last_touch_ms <= 0 || (now_ms - last_touch_ms) > it->second.stale_after_ms) {
                it = osd_overrides.erase(it);
            } else {
                ++it;
            }
        }
    }

    void init_encoder() {
        if (obj_enc_ctx) {
            return;
        }

        obj_enc_ctx = nvds_obj_enc_create_context(0);
        if (!obj_enc_ctx) {
            LOG_E("FrameEventsExtProcService: failed to create NvDsObjEnc context");
        }
    }

    void destroy_encoder() {
        if (obj_enc_ctx) {
            nvds_obj_enc_destroy_context(obj_enc_ctx);
            obj_enc_ctx = nullptr;
        }
    }

    std::vector<unsigned char> encode_object_jpeg(NvDsObjectMeta* obj_meta,
                                                  NvDsFrameMeta* frame_meta,
                                                  NvBufSurface* batch_surf, int jpeg_quality) {
        if (!obj_enc_ctx) {
            return {};
        }

        NvDsObjEncUsrArgs enc_args{};
        enc_args.saveImg = FALSE;
        enc_args.attachUsrMeta = TRUE;
        enc_args.quality = jpeg_quality;

        nvds_obj_enc_process(obj_enc_ctx, &enc_args, batch_surf, obj_meta, frame_meta);
        nvds_obj_enc_finish(obj_enc_ctx);

        for (NvDsMetaList* ul = obj_meta->obj_user_meta_list; ul; ul = ul->next) {
            auto* um = static_cast<NvDsUserMeta*>(ul->data);
            if (um && um->base_meta.meta_type == NVDS_CROP_IMAGE_META) {
                auto* enc_out = static_cast<NvDsObjEncOutParams*>(um->user_meta_data);
                if (enc_out && enc_out->outBuffer && enc_out->outLen > 0) {
                    return std::vector<unsigned char>(enc_out->outBuffer,
                                                      enc_out->outBuffer + enc_out->outLen);
                }
            }
        }

        LOG_W("FrameEventsExtProcService: JPEG encode succeeded but no output meta found");
        return {};
    }

    void perform_api_call(const std::string& handler_id, const RegisteredHandler& handler,
                          std::vector<unsigned char> jpeg_bytes,
                          engine::core::config::FrameEventsExtProcRule rule, int source_id,
                          const std::string& source_name, const std::string& frame_key,
                          int64_t frame_ts_ms, const std::string& overview_ref,
                          const std::string& crop_ref, const std::string& object_key,
                          const std::string& instance_key, uint64_t tracker_id, int class_id,
                          const std::string& object_type, double confidence) {
        CURL* curl = curl_easy_init();
        if (!curl) {
            LOG_W("FrameEventsExtProcService: curl_easy_init failed for publish_channel='{}'",
                  handler.config.publish_channel);
            return;
        }

        std::string url = rule.endpoint;
        if (!rule.params.empty()) {
            url.push_back('?');
            bool first = true;
            for (const auto& [key, value] : rule.params) {
                if (!first) {
                    url.push_back('&');
                }
                char* escaped_key =
                    curl_easy_escape(curl, key.c_str(), static_cast<int>(key.size()));
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
            LOG_W("FrameEventsExtProcService: HTTP error object_key='{}': {}", object_key,
                  curl_easy_strerror(curl_code));
            return;
        }

        FrameEventsExtProcResult result;
        try {
            const auto parsed = json::parse(response_body);
            result.result = json_get_by_path(parsed, rule.result_path);
            result.display = json_get_by_path(parsed, rule.display_path);
        } catch (const std::exception& ex) {
            LOG_W("FrameEventsExtProcService: JSON parse error object_key='{}': {}", object_key,
                  ex.what());
            return;
        }

        if (result.result.empty() && !handler.config.emit_empty_result) {
            return;
        }
        if (result.result.empty()) {
            result.status = "empty_result";
        }

        const std::string osd_text = result.display.empty() ? result.result : result.display;
        if (handler.config.override_osd_text && !osd_text.empty()) {
            const int64_t now_ms = monotonic_ms();
            std::lock_guard<std::mutex> lk(mutex);
            auto& entry = osd_overrides[make_osd_override_key(handler_id, source_id, tracker_id)];
            entry.text = osd_text;
            entry.object_type = object_type;
            entry.updated_at_ms = now_ms;
            entry.last_seen_at_ms = now_ms;
            entry.stale_after_ms = std::max<int64_t>(
                kMinimumOsdOverrideRetentionMs,
                std::max<int64_t>(kDefaultOsdOverrideRetentionMs,
                                  static_cast<int64_t>(handler.config.request_timeout_ms) * 3));
            cleanup_stale_osd_overrides_locked(now_ms);
        }

        if (!producer || handler.config.publish_channel.empty()) {
            return;
        }

        json message = json::object();
        message["event"] = "ext_proc";
        message["status"] = result.status;
        message["pid"] = handler.pipeline_id;
        message["pipeline_id"] = handler.pipeline_id;
        message["sid"] = source_id;
        message["source_id"] = source_id;
        message["sname"] = source_name;
        message["source_name"] = source_name;
        message["frame_key"] = frame_key;
        message["frame_ts_ms"] = frame_ts_ms;
        message["instance_key"] = instance_key;
        message["oid"] = tracker_id;
        message["tracker_id"] = tracker_id;
        message["object_key"] = object_key;
        message["class"] = object_type;
        message["class_id"] = class_id;
        message["conf"] = confidence;
        message["labels"] =
            result.display.empty() ? result.result : result.result + "|" + result.display;
        message["result"] = result.result;
        message["display"] = result.display;
        message["crop_ref"] = crop_ref;
        if (handler.config.include_overview_ref) {
            message["overview_ref"] = overview_ref;
        }
        message["event_ts"] = std::to_string(epoch_ms());

        producer->publish_json(handler.config.publish_channel, message.dump());
    }
};

FrameEventsExtProcService::FrameEventsExtProcService(
    engine::core::messaging::IMessageProducer* producer)
    : pimpl_(std::make_shared<Impl>()) {
    pimpl_->producer = producer;
}

FrameEventsExtProcService::~FrameEventsExtProcService() {
    if (pimpl_) {
        pimpl_->destroy_encoder();
    }
}

bool FrameEventsExtProcService::register_handler(
    const std::string& handler_id, const std::string& pipeline_id,
    const engine::core::config::FrameEventsExtProcConfig& config) {
    if (handler_id.empty() || !config.enable || config.publish_channel.empty() ||
        config.rules.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lk(pimpl_->mutex);
    pimpl_->handlers[handler_id] = Impl::RegisteredHandler{pipeline_id, config};

    LOG_I(
        "FrameEventsExtProcService: registered handler='{}' channel='{}' jpeg_quality={} "
        "connect_timeout_ms={} request_timeout_ms={} rules={}",
        handler_id, config.publish_channel, config.jpeg_quality, config.connect_timeout_ms,
        config.request_timeout_ms, config.rules.size());
    return true;
}

bool FrameEventsExtProcService::start() {
    std::lock_guard<std::mutex> lk(pimpl_->mutex);
    if (pimpl_->started) {
        return true;
    }
    if (!pimpl_->producer || pimpl_->handlers.empty()) {
        return false;
    }

    pimpl_->init_encoder();
    pimpl_->started = pimpl_->obj_enc_ctx != nullptr;
    return pimpl_->started;
}

void FrameEventsExtProcService::stop() {
    std::lock_guard<std::mutex> lk(pimpl_->mutex);
    pimpl_->started = false;
    pimpl_->destroy_encoder();
}

bool FrameEventsExtProcService::is_running() const {
    std::lock_guard<std::mutex> lk(pimpl_->mutex);
    return pimpl_->started && pimpl_->obj_enc_ctx != nullptr;
}

void FrameEventsExtProcService::apply_cached_display_text(const std::string& handler_id,
                                                          int source_id,
                                                          NvDsFrameMeta* frame_meta) {
    if (!frame_meta) {
        return;
    }

    const int64_t now_ms = monotonic_ms();
    std::lock_guard<std::mutex> lk(pimpl_->mutex);
    const auto handler_it = pimpl_->handlers.find(handler_id);
    if (handler_it == pimpl_->handlers.end() || !handler_it->second.config.override_osd_text) {
        return;
    }

    pimpl_->cleanup_stale_osd_overrides_locked(now_ms);

    for (NvDsMetaList* object_iter = frame_meta->obj_meta_list; object_iter;
         object_iter = object_iter->next) {
        auto* object_meta = static_cast<NvDsObjectMeta*>(object_iter->data);
        if (!object_meta) {
            continue;
        }

        const uint64_t tracker_id = static_cast<uint64_t>(object_meta->object_id);
        const auto override_it =
            pimpl_->osd_overrides.find(make_osd_override_key(handler_id, source_id, tracker_id));
        if (override_it == pimpl_->osd_overrides.end()) {
            continue;
        }

        const std::string current_object_type =
            object_meta->obj_label ? object_meta->obj_label : "";
        if (!override_it->second.object_type.empty() && !current_object_type.empty() &&
            override_it->second.object_type != current_object_type) {
            continue;
        }

        override_it->second.last_seen_at_ms = now_ms;
        apply_object_display_text(object_meta, override_it->second.text);
    }
}

void FrameEventsExtProcService::process_object(
    const std::string& handler_id, int source_id, const std::string& source_name,
    const std::string& frame_key, int64_t frame_ts_ms, const std::string& overview_ref,
    const std::string& crop_ref, const std::string& object_key, const std::string& instance_key,
    uint64_t /*object_id*/, uint64_t tracker_id, int class_id, const std::string& object_type,
    double confidence, NvDsObjectMeta* obj_meta, NvDsFrameMeta* frame_meta,
    NvBufSurface* batch_surf) {
    if (!obj_meta || !frame_meta || !batch_surf) {
        return;
    }

    Impl::RegisteredHandler handler;
    {
        std::lock_guard<std::mutex> lk(pimpl_->mutex);
        if (!pimpl_->started || !pimpl_->obj_enc_ctx) {
            return;
        }

        const auto handler_it = pimpl_->handlers.find(handler_id);
        if (handler_it == pimpl_->handlers.end()) {
            return;
        }
        handler = handler_it->second;
    }

    const auto* rule_ptr = find_rule(handler.config, object_type);
    if (!rule_ptr) {
        return;
    }
    const auto rule = *rule_ptr;

    auto jpeg_bytes =
        pimpl_->encode_object_jpeg(obj_meta, frame_meta, batch_surf, handler.config.jpeg_quality);
    if (jpeg_bytes.empty()) {
        LOG_W("FrameEventsExtProcService: JPEG encode failed object_key='{}' frame_key='{}'",
              object_key, frame_key);
        return;
    }

    auto impl_ref = pimpl_;
    // Copy the matched rule into the detached worker thread; the local handler copy dies when
    // process_object() returns, so handing off a raw pointer here would dangle.
    std::thread([impl_ref = std::move(impl_ref), handler_id, handler, rule,
                 jpeg = std::move(jpeg_bytes), source_id, source_name, frame_key, frame_ts_ms,
                 overview_ref, crop_ref, object_key, instance_key, tracker_id, class_id,
                 object_type, confidence]() mutable {
        impl_ref->perform_api_call(handler_id, handler, std::move(jpeg), rule, source_id,
                                   source_name, frame_key, frame_ts_ms, overview_ref, crop_ref,
                                   object_key, instance_key, tracker_id, class_id, object_type,
                                   confidence);
    }).detach();
}

}  // namespace engine::pipeline::extproc