// pipeline/src/extproc/ext_proc_svc.cpp
#include "engine/pipeline/extproc/ext_proc_svc.hpp"
#include "engine/core/utils/logger.hpp"

#include <gstnvdsmeta.h>
#include <nvbufsurface.h>
#include <nvds_obj_encode.h>
#include <nlohmann/json.hpp>
#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
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

std::string json_get_by_path(const json& j, const std::string& path) {
    if (path.empty())
        return "";
    try {
        std::string ptr;
        ptr.reserve(path.size() + 1);
        ptr += '/';
        for (char c : path)
            ptr += (c == '.') ? '/' : c;
        const auto& val = j.at(json::json_pointer(ptr));
        return val.is_string() ? val.get<std::string>() : val.dump();
    } catch (...) {
        return "";
    }
}

inline int64_t monotonic_ns() noexcept {
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch())
                                    .count());
}

inline int64_t epoch_ms() noexcept {
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count());
}

}  // namespace

struct ExternalProcessorService::Impl {
    engine::core::config::ExtProcessorConfig config;
    std::string pipeline_id;
    engine::core::messaging::IMessageProducer* producer = nullptr;
    std::string channel;
    std::unordered_map<std::string, const engine::core::config::ExtProcessorRule*> rule_map;
    NvDsObjEncCtxHandle obj_enc_ctx = nullptr;
    std::mutex throttle_mutex;
    std::unordered_map<std::string, int64_t> last_processed_ns;

    void init_encoder() {
        obj_enc_ctx = nvds_obj_enc_create_context(0);
        if (!obj_enc_ctx) {
            LOG_E("ExternalProcessorService: failed to create NvDsObjEnc context");
        }
    }

    void destroy_encoder() {
        if (obj_enc_ctx) {
            nvds_obj_enc_destroy_context(obj_enc_ctx);
            obj_enc_ctx = nullptr;
        }
    }

    bool should_process(const std::string& key) {
        const int64_t min_ns = static_cast<int64_t>(config.min_interval_sec) * 1'000'000'000LL;
        const int64_t now = monotonic_ns();
        std::lock_guard<std::mutex> lk(throttle_mutex);
        auto it = last_processed_ns.find(key);
        if (it != last_processed_ns.end()) {
            if ((now - it->second) < min_ns)
                return false;
            it->second = now;
        } else {
            last_processed_ns.emplace(key, now);
        }
        return true;
    }

    std::vector<unsigned char> encode_object_jpeg(NvDsObjectMeta* obj_meta,
                                                  NvDsFrameMeta* frame_meta,
                                                  NvBufSurface* batch_surf) {
        if (!obj_enc_ctx)
            return {};

        NvDsObjEncUsrArgs enc_args{};
        enc_args.saveImg = FALSE;
        enc_args.attachUsrMeta = TRUE;
        enc_args.quality = 80;

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
        LOG_W("ExternalProcessorService: JPEG encode succeeded but no output meta found");
        return {};
    }

    void perform_api_call(std::vector<unsigned char> jpeg_data,
                          const engine::core::config::ExtProcessorRule* rule, int source_id,
                          const std::string& source_name, const std::string& instance_key,
                          const std::string& object_key, uint64_t tracker_id, int class_id,
                          const std::string& label, float confidence) {
        CURL* curl = curl_easy_init();
        if (!curl) {
            LOG_E("ExternalProcessorService: curl_easy_init() failed for label='{}'", label);
            return;
        }

        std::string url = rule->endpoint;
        if (!rule->params.empty()) {
            url += '?';
            bool first = true;
            for (const auto& [k, v] : rule->params) {
                if (!first)
                    url += '&';
                char* ek = curl_easy_escape(curl, k.c_str(), static_cast<int>(k.size()));
                char* ev = curl_easy_escape(curl, v.c_str(), static_cast<int>(v.size()));
                url += std::string(ek) + '=' + std::string(ev);
                curl_free(ek);
                curl_free(ev);
                first = false;
            }
        }

        curl_mime* mime = curl_mime_init(curl);
        curl_mimepart* part = curl_mime_addpart(mime);
        curl_mime_name(part, "file");
        curl_mime_filename(part, "image.jpg");
        curl_mime_type(part, "image/jpeg");
        curl_mime_data(part, reinterpret_cast<const char*>(jpeg_data.data()), jpeg_data.size());

        std::string response_body;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "ExtProcSvc/1.0");

        CURLcode res = curl_easy_perform(curl);
        curl_mime_free(mime);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            LOG_W("ExternalProcessorService: HTTP error label='{}': {}", label,
                  curl_easy_strerror(res));
            return;
        }

        std::string result_val;
        std::string display_val;
        try {
            const auto j = json::parse(response_body);
            result_val = json_get_by_path(j, rule->result_path);
            display_val = json_get_by_path(j, rule->display_path);
        } catch (const std::exception& e) {
            LOG_W("ExternalProcessorService: JSON parse error for label='{}': {}", label, e.what());
        }

        if (result_val.empty()) {
            LOG_D("ExternalProcessorService: skip publish ext_proc (empty result) label='{}'",
                  label);
            return;
        }

        if (!producer || channel.empty())
            return;

        std::string labels_str = result_val;
        if (!display_val.empty()) {
            labels_str += '|';
            labels_str += display_val;
        }

        json msg;
        msg["event"] = "ext_proc";
        msg["pid"] = pipeline_id;
        msg["sid"] = source_id;
        msg["sname"] = source_name;
        msg["instance_key"] = instance_key;
        msg["oid"] = tracker_id;
        msg["object_key"] = object_key;
        msg["parent_object_key"] = "";
        msg["parent"] = "";
        msg["parent_instance_key"] = "";
        msg["class"] = label;
        msg["class_id"] = class_id;
        msg["conf"] = confidence;
        msg["labels"] = labels_str;
        msg["result"] = result_val;
        msg["display"] = display_val;
        msg["top"] = "";
        msg["left"] = "";
        msg["w"] = "";
        msg["h"] = "";
        msg["s_w_ff"] = "";
        msg["s_h_ff"] = "";
        msg["w_ff"] = "";
        msg["h_ff"] = "";
        msg["fname"] = "";
        msg["fname_ff"] = "";
        msg["event_ts"] = std::to_string(epoch_ms());

        try {
            producer->publish_json(channel, msg.dump());
            LOG_I(
                "ExternalProcessorService: published ext_proc "
                "label='{}' result='{}' display='{}'",
                label, result_val, display_val);
        } catch (const std::exception& e) {
            LOG_W("ExternalProcessorService: publish error label='{}': {}", label, e.what());
        }
    }
};

ExternalProcessorService::ExternalProcessorService() : pimpl_(std::make_shared<Impl>()) {}

ExternalProcessorService::~ExternalProcessorService() {
    if (pimpl_) {
        pimpl_->destroy_encoder();
    }
}

void ExternalProcessorService::configure(const engine::core::config::ExtProcessorConfig& config,
                                         const std::string& pipeline_id,
                                         engine::core::messaging::IMessageProducer* producer,
                                         const std::string& channel) {
    pimpl_->config = config;
    pimpl_->pipeline_id = pipeline_id;
    pimpl_->producer = producer;
    pimpl_->channel = channel;

    pimpl_->rule_map.clear();
    for (const auto& rule : pimpl_->config.rules) {
        pimpl_->rule_map[rule.label] = &rule;
        LOG_I("ExternalProcessorService: rule label='{}' endpoint='{}'", rule.label, rule.endpoint);
    }

    pimpl_->init_encoder();

    LOG_I("ExternalProcessorService: configured — {} rules, min_interval={}s, channel='{}'",
          pimpl_->config.rules.size(), pimpl_->config.min_interval_sec, channel);
}

void ExternalProcessorService::process_object(NvDsObjectMeta* obj_meta, NvDsFrameMeta* frame_meta,
                                              NvBufSurface* batch_surf,
                                              const std::string& source_name,
                                              const std::string& instance_key,
                                              const std::string& object_key) {
    if (!pimpl_->obj_enc_ctx)
        return;
    if (!obj_meta || !frame_meta || !batch_surf)
        return;

    const std::string label(obj_meta->obj_label);
    auto it = pimpl_->rule_map.find(label);
    if (it == pimpl_->rule_map.end())
        return;

    const auto* rule = it->second;
    const int source_id = static_cast<int>(frame_meta->source_id);
    const auto tracker_id = static_cast<uint64_t>(obj_meta->object_id);

    const std::string throttle_key =
        std::to_string(source_id) + ':' + std::to_string(tracker_id) + ':' + label;

    if (!pimpl_->should_process(throttle_key)) {
        LOG_T("ExternalProcessorService: throttled key='{}'", throttle_key);
        return;
    }

    auto jpeg_data = pimpl_->encode_object_jpeg(obj_meta, frame_meta, batch_surf);
    if (jpeg_data.empty()) {
        LOG_W("ExternalProcessorService: JPEG encode failed for label='{}' tracker_id={}", label,
              tracker_id);
        return;
    }

    LOG_D("ExternalProcessorService: launching API call label='{}' jpeg={}B tracker_id={}", label,
          jpeg_data.size(), tracker_id);

    auto impl_ref = pimpl_;
    const int class_id = obj_meta->class_id;
    const float conf = obj_meta->confidence;
    const std::string lbl = label;

    std::thread([impl_ref = std::move(impl_ref), jpeg = std::move(jpeg_data), rule, source_id,
                 source_name, instance_key, object_key, tracker_id, class_id, lbl, conf]() mutable {
        impl_ref->perform_api_call(std::move(jpeg), rule, source_id, source_name, instance_key,
                                   object_key, tracker_id, class_id, lbl, conf);
    }).detach();
}

}  // namespace engine::pipeline::extproc