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

size_t curl_write_callback(void* contents, size_t size, size_t nmemb, std::string* out) noexcept {
    out->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
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

}  // namespace

struct FrameEventsExtProcService::Impl {
    struct RegisteredHandler {
        std::string pipeline_id;
        engine::core::config::FrameEventsExtProcConfig config;
    };

    engine::core::messaging::IMessageProducer* producer = nullptr;
    std::mutex mutex;
    std::unordered_map<std::string, RegisteredHandler> handlers;
    NvDsObjEncCtxHandle obj_enc_ctx = nullptr;
    bool started = false;

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

    void perform_api_call(const RegisteredHandler& handler, std::vector<unsigned char> jpeg_bytes,
                          engine::core::config::FrameEventsExtProcRule rule, int source_id,
                          const std::string& source_name, const std::string& frame_key,
                          int64_t frame_ts_ms, const std::string& overview_ref,
                          const std::string& crop_ref, const std::string& object_key,
                          const std::string& instance_key, uint64_t tracker_id, int class_id,
                          const std::string& object_type, double confidence) {
        if (!producer || handler.config.publish_channel.empty()) {
            return;
        }

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
    std::thread([impl_ref = std::move(impl_ref), handler, rule, jpeg = std::move(jpeg_bytes),
                 source_id, source_name, frame_key, frame_ts_ms, overview_ref, crop_ref, object_key,
                 instance_key, tracker_id, class_id, object_type, confidence]() mutable {
        impl_ref->perform_api_call(handler, std::move(jpeg), rule, source_id, source_name,
                                   frame_key, frame_ts_ms, overview_ref, crop_ref, object_key,
                                   instance_key, tracker_id, class_id, object_type, confidence);
    }).detach();
}

}  // namespace engine::pipeline::extproc