#include "engine/pipeline/runtime_stream_manager.hpp"
#include "engine/pipeline/builders/nvurisrcbin_builder.hpp"
#include "engine/pipeline/source_naming.hpp"
#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

#include <algorithm>

using engine::core::pipeline::RuntimeSourceErrorCode;
using engine::core::pipeline::RuntimeSourceInfo;
using engine::core::pipeline::RuntimeSourceMutationResult;

namespace engine::pipeline {

namespace {

constexpr const char* kLinkContextDataKey = "engine_mux_link_context";

struct SourceLinkContext {
    GstPad* mux_sink_pad = nullptr;
    std::string camera_id;
    bool linked = false;
};

RuntimeSourceMutationResult make_result(bool success, int http_status,
                                        RuntimeSourceErrorCode error_code,
                                        const std::string& camera_id, const std::string& message) {
    RuntimeSourceMutationResult result;
    result.success = success;
    result.http_status = http_status;
    result.error_code = error_code;
    result.camera_id = camera_id;
    result.message = message;
    return result;
}

bool wait_for_element_state(GstElement* element, GstState target, GstClockTime timeout) {
    if (element == nullptr) {
        return false;
    }

    GstState current = GST_STATE_VOID_PENDING;
    GstState pending = GST_STATE_VOID_PENDING;
    const GstStateChangeReturn state_result =
        gst_element_get_state(element, &current, &pending, timeout);
    if (state_result == GST_STATE_CHANGE_FAILURE) {
        return false;
    }

    return current == target || pending == target || state_result == GST_STATE_CHANGE_SUCCESS;
}

void destroy_link_context(gpointer data) {
    auto* context = static_cast<SourceLinkContext*>(data);
    if (context != nullptr && context->mux_sink_pad != nullptr) {
        gst_object_unref(context->mux_sink_pad);
    }
    delete context;
}

bool link_source_pad_to_mux(GstPad* source_pad, SourceLinkContext* context) {
    if (source_pad == nullptr || context == nullptr || context->mux_sink_pad == nullptr) {
        return false;
    }

    if (context->linked || gst_pad_is_linked(context->mux_sink_pad)) {
        context->linked = true;
        return true;
    }

    const GstPadLinkReturn link_result = gst_pad_link(source_pad, context->mux_sink_pad);
    if (link_result != GST_PAD_LINK_OK) {
        LOG_E("RuntimeStreamManager: failed to link '{}' to mux for camera '{}' ({})",
              GST_PAD_NAME(source_pad), context->camera_id, gst_pad_link_get_name(link_result));
        return false;
    }

    context->linked = true;
    LOG_I("RuntimeStreamManager: linked camera '{}' into nvstreammux", context->camera_id);
    return true;
}

void on_source_pad_added(GstElement* source, GstPad* new_pad, gpointer user_data) {
    auto* context = static_cast<SourceLinkContext*>(user_data);
    if (context == nullptr || gst_pad_get_direction(new_pad) != GST_PAD_SRC) {
        return;
    }

    link_source_pad_to_mux(new_pad, context);

    const gchar* element_name = source != nullptr ? GST_ELEMENT_NAME(source) : "unknown";
    LOG_D("RuntimeStreamManager: pad-added handled for '{}': {}", element_name,
          GST_PAD_NAME(new_pad));
}

}  // namespace

RuntimeStreamManager::RuntimeStreamManager(GstElement* source_root, GstElement* muxer,
                                           engine::core::config::SourcesConfig sources_config)
    : source_root_(source_root != nullptr ? GST_ELEMENT(gst_object_ref(source_root)) : nullptr),
      muxer_(muxer != nullptr ? GST_ELEMENT(gst_object_ref(muxer)) : nullptr),
      sources_config_(std::move(sources_config)) {
    seed_existing_streams();
}

RuntimeStreamManager::~RuntimeStreamManager() {
    for (auto& [camera_id, slot] : streams_) {
        if (slot.source != nullptr) {
            gst_object_unref(slot.source);
        }
        if (slot.pad_signal_source != nullptr) {
            gst_object_unref(slot.pad_signal_source);
        }
        if (slot.mux_sink_pad != nullptr) {
            gst_object_unref(slot.mux_sink_pad);
        }
    }
    streams_.clear();

    if (source_root_ != nullptr) {
        gst_object_unref(source_root_);
    }
    if (muxer_ != nullptr) {
        gst_object_unref(muxer_);
    }
}

bool RuntimeStreamManager::add_stream(const engine::core::config::CameraConfig& camera) {
    return add_stream_detailed(camera).success;
}

RuntimeSourceMutationResult RuntimeStreamManager::add_stream_detailed(
    const engine::core::config::CameraConfig& camera) {
    std::lock_guard<std::mutex> lock(mtx_);

    if (camera.id.empty() || camera.uri.empty()) {
        LOG_E("RuntimeStreamManager: camera id and uri are required for add_stream");
        return make_result(false, 400, RuntimeSourceErrorCode::InvalidRequest, camera.id,
                           "camera id and uri are required");
    }

    if (source_root_ == nullptr || muxer_ == nullptr) {
        LOG_E("RuntimeStreamManager: source root or muxer is null — cannot add '{}'", camera.id);
        return make_result(false, 500, RuntimeSourceErrorCode::InternalError, camera.id,
                           "runtime stream manager is unavailable");
    }

    if (streams_.find(camera.id) != streams_.end()) {
        LOG_W("RuntimeStreamManager: camera '{}' already exists", camera.id);
        return make_result(false, 409, RuntimeSourceErrorCode::DuplicateCameraId, camera.id,
                           "camera id already exists");
    }

    const int max_sources = sources_config_.mux.max_sources > 0 ? sources_config_.mux.max_sources
                                                                : sources_config_.max_batch_size;
    if (static_cast<int>(streams_.size()) >= max_sources) {
        LOG_E("RuntimeStreamManager: cannot add '{}' because max_sources={} is reached", camera.id,
              max_sources);
        return make_result(false, 507, RuntimeSourceErrorCode::MaxSourcesReached, camera.id,
                           "maximum active sources reached");
    }

    const uint32_t source_index = allocate_source_index();
    const std::string source_bin_name = make_source_bin_name(camera.id);
    const bool has_branch = std::any_of(
        sources_config_.branch.elements.begin(), sources_config_.branch.elements.end(),
        [](const engine::core::config::SourceBranchElementConfig& cfg) { return cfg.enabled; });

    const std::string sink_pad_name = "sink_" + std::to_string(source_index);
    GstPad* mux_sink_pad = gst_element_request_pad_simple(muxer_, sink_pad_name.c_str());
    if (mux_sink_pad == nullptr) {
        release_source_index(source_index);
        LOG_E("RuntimeStreamManager: failed to request nvstreammux pad '{}'", sink_pad_name);
        return make_result(false, 500, RuntimeSourceErrorCode::RequestPadFailed, camera.id,
                           "failed to request nvstreammux sink pad");
    }

    builders::NvUriSrcBinBuilder source_builder(source_root_);
    GstElement* source = source_builder.build(sources_config_, camera, source_index);
    if (source == nullptr) {
        gst_element_release_request_pad(muxer_, mux_sink_pad);
        gst_object_unref(mux_sink_pad);
        release_source_index(source_index);
        LOG_E("RuntimeStreamManager: failed to build source bin '{}'", source_bin_name);
        return make_result(false, 500, RuntimeSourceErrorCode::BuildSourceFailed, camera.id,
                           "failed to build source bin");
    }

    GstElement* pad_signal_source = nullptr;
    gulong pad_added_handler_id = 0;
    auto* link_context =
        new SourceLinkContext{GST_PAD(gst_object_ref(mux_sink_pad)), camera.id, false};
    bool link_context_attached = false;

    const auto cleanup_failed_add = [&](RuntimeSourceErrorCode error_code,
                                        const std::string& message) {
        if (pad_signal_source != nullptr) {
            gst_object_unref(pad_signal_source);
            pad_signal_source = nullptr;
        }
        if (!link_context_attached) {
            delete link_context;
        }
        if (source != nullptr && GST_OBJECT_PARENT(source) == GST_OBJECT(source_root_)) {
            gst_bin_remove(GST_BIN(source_root_), source);
        }
        gst_element_release_request_pad(muxer_, mux_sink_pad);
        gst_object_unref(mux_sink_pad);
        release_source_index(source_index);
        return make_result(false, 500, error_code, camera.id, message);
    };

    if (has_branch) {
        GObject* source_obj = G_OBJECT(source);
        g_object_set_data_full(source_obj, kLinkContextDataKey, link_context, destroy_link_context);
        link_context_attached = true;
        GstPad* source_pad = gst_element_get_static_pad(source, "src");
        if (source_pad != nullptr) {
            if (!link_source_pad_to_mux(source_pad, link_context)) {
                gst_object_unref(source_pad);
                LOG_E("RuntimeStreamManager: failed to link source-bin '{}' to mux",
                      source_bin_name);
                return cleanup_failed_add(RuntimeSourceErrorCode::LinkSourceFailed,
                                          "failed to link source bin to mux");
            }
            gst_object_unref(source_pad);
        }
    } else {
        pad_signal_source =
            gst_bin_get_by_name(GST_BIN(source), make_source_element_name(camera.id).c_str());
        if (pad_signal_source == nullptr) {
            LOG_E("RuntimeStreamManager: failed to resolve raw nvurisrcbin inside '{}'",
                  source_bin_name);
            return cleanup_failed_add(RuntimeSourceErrorCode::BuildSourceFailed,
                                      "failed to resolve nvurisrcbin inside source bin");
        }

        g_object_set_data_full(G_OBJECT(pad_signal_source), kLinkContextDataKey, link_context,
                               destroy_link_context);
        link_context_attached = true;
        pad_added_handler_id = g_signal_connect(pad_signal_source, "pad-added",
                                                G_CALLBACK(on_source_pad_added), link_context);

        GstPad* source_pad = gst_element_get_static_pad(pad_signal_source, "src");
        if (source_pad != nullptr) {
            if (!link_source_pad_to_mux(source_pad, link_context)) {
                gst_object_unref(source_pad);
                LOG_E("RuntimeStreamManager: failed to link raw source '{}' to mux", camera.id);
                return cleanup_failed_add(RuntimeSourceErrorCode::LinkSourceFailed,
                                          "failed to link source pad to mux");
            }
            gst_object_unref(source_pad);
        }
    }

    if (GST_OBJECT_PARENT(source_root_) != nullptr) {
        if (!gst_element_sync_state_with_parent(source)) {
            LOG_E("RuntimeStreamManager: failed to sync '{}' with parent", source_bin_name);
            return cleanup_failed_add(RuntimeSourceErrorCode::BuildSourceFailed,
                                      "failed to sync source with parent state");
        }
        if (!wait_for_element_state(source, GST_STATE_PLAYING, 5 * GST_SECOND)) {
            LOG_W("RuntimeStreamManager: source '{}' did not settle in PLAYING within timeout",
                  source_bin_name);
        }
    }

    StreamSlot slot;
    slot.source_index = source_index;
    slot.camera_id = camera.id;
    slot.camera_uri = camera.uri;
    slot.is_seeded = false;
    slot.state = "active";
    slot.source = GST_ELEMENT(gst_object_ref(source));
    slot.pad_signal_source =
        pad_signal_source != nullptr ? GST_ELEMENT(gst_object_ref(pad_signal_source)) : nullptr;
    slot.mux_sink_pad = mux_sink_pad;
    slot.pad_added_handler_id = pad_added_handler_id;
    streams_.emplace(camera.id, slot);
    if (pad_signal_source != nullptr) {
        gst_object_unref(pad_signal_source);
    }
    LOG_I("RuntimeStreamManager: added camera '{}' on mux pad '{}', uri='{}'", camera.id,
          sink_pad_name, camera.uri);

    RuntimeSourceMutationResult result =
        make_result(true, 201, RuntimeSourceErrorCode::None, camera.id, "camera added");
    result.source_index = static_cast<int>(source_index);
    result.active_source_count = static_cast<int>(streams_.size());
    result.source = RuntimeSourceInfo{camera.id, camera.uri, source_index, false, "active"};
    return result;
}

bool RuntimeStreamManager::remove_stream(const std::string& camera_id) {
    return remove_stream_detailed(camera_id).success;
}

RuntimeSourceMutationResult RuntimeStreamManager::remove_stream_detailed(
    const std::string& camera_id) {
    std::lock_guard<std::mutex> lock(mtx_);

    if (source_root_ == nullptr || muxer_ == nullptr) {
        LOG_E("RuntimeStreamManager: source root or muxer is null — cannot remove '{}'", camera_id);
        return make_result(false, 500, RuntimeSourceErrorCode::InternalError, camera_id,
                           "runtime stream manager is unavailable");
    }

    auto it = streams_.find(camera_id);
    if (it == streams_.end()) {
        LOG_W("RuntimeStreamManager: stream '{}' not found", camera_id);
        return make_result(false, 404, RuntimeSourceErrorCode::CameraNotFound, camera_id,
                           "camera id is not active");
    }

    StreamSlot slot = it->second;
    RuntimeSourceInfo removed_source{slot.camera_id, slot.camera_uri, slot.source_index,
                                     slot.is_seeded, slot.state};

    if (slot.pad_signal_source != nullptr && slot.pad_added_handler_id != 0) {
        g_signal_handler_disconnect(slot.pad_signal_source, slot.pad_added_handler_id);
    }

    if (slot.source != nullptr) {
        gst_element_set_state(slot.source, GST_STATE_READY);
        if (!wait_for_element_state(slot.source, GST_STATE_READY, 5 * GST_SECOND)) {
            LOG_W("RuntimeStreamManager: source '{}' did not settle in READY within timeout",
                  camera_id);
        }
    }

    if (slot.mux_sink_pad != nullptr) {
        GstPad* peer_pad = gst_pad_get_peer(slot.mux_sink_pad);
        if (peer_pad != nullptr) {
            gst_pad_unlink(peer_pad, slot.mux_sink_pad);
            gst_object_unref(peer_pad);
        }

        gst_element_release_request_pad(muxer_, slot.mux_sink_pad);
        gst_object_unref(slot.mux_sink_pad);
        slot.mux_sink_pad = nullptr;
    }

    if (slot.source != nullptr) {
        gst_element_set_state(slot.source, GST_STATE_NULL);
        if (!wait_for_element_state(slot.source, GST_STATE_NULL, 5 * GST_SECOND)) {
            LOG_W("RuntimeStreamManager: source '{}' did not settle in NULL within timeout",
                  camera_id);
        }
    }

    if (slot.source != nullptr) {
        gst_bin_remove(GST_BIN(source_root_), slot.source);
        gst_object_unref(slot.source);
        slot.source = nullptr;
    }

    if (slot.pad_signal_source != nullptr) {
        gst_object_unref(slot.pad_signal_source);
        slot.pad_signal_source = nullptr;
    }

    release_source_index(it->second.source_index);
    streams_.erase(it);
    LOG_I("RuntimeStreamManager: removed camera '{}'", camera_id);

    RuntimeSourceMutationResult result =
        make_result(true, 200, RuntimeSourceErrorCode::None, camera_id, "camera removed");
    result.source_index = static_cast<int>(removed_source.source_index);
    result.active_source_count = static_cast<int>(streams_.size());
    result.source = removed_source;
    return result;
}

std::vector<RuntimeSourceInfo> RuntimeStreamManager::list_streams() const {
    std::lock_guard<std::mutex> lock(mtx_);

    std::vector<RuntimeSourceInfo> sources;
    sources.reserve(streams_.size());
    for (const auto& [camera_id, slot] : streams_) {
        sources.push_back(
            RuntimeSourceInfo{camera_id, slot.camera_uri, slot.source_index, slot.is_seeded,
                              slot.state.empty() ? std::string("active") : slot.state});
    }

    std::sort(sources.begin(), sources.end(),
              [](const RuntimeSourceInfo& lhs, const RuntimeSourceInfo& rhs) {
                  if (lhs.source_index != rhs.source_index) {
                      return lhs.source_index < rhs.source_index;
                  }
                  return lhs.camera_id < rhs.camera_id;
              });
    return sources;
}

int RuntimeStreamManager::get_active_stream_count() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return static_cast<int>(streams_.size());
}

uint32_t RuntimeStreamManager::allocate_source_index() {
    if (!free_source_indexes_.empty()) {
        const uint32_t source_index = free_source_indexes_.back();
        free_source_indexes_.pop_back();
        return source_index;
    }

    return next_source_index_++;
}

void RuntimeStreamManager::release_source_index(uint32_t source_index) {
    free_source_indexes_.push_back(source_index);
    std::sort(free_source_indexes_.begin(), free_source_indexes_.end(), std::greater<uint32_t>());
}

void RuntimeStreamManager::seed_existing_streams() {
    if (source_root_ == nullptr || muxer_ == nullptr) {
        return;
    }

    uint32_t max_existing_index = 0;
    bool found_existing = false;

    for (std::size_t index = 0; index < sources_config_.cameras.size(); ++index) {
        const auto& camera = sources_config_.cameras[index];
        if (camera.id.empty()) {
            continue;
        }

        GstElement* source =
            gst_bin_get_by_name(GST_BIN(source_root_), make_source_bin_name(camera.id).c_str());
        if (source == nullptr) {
            continue;
        }

        GstPad* mux_sink_pad =
            gst_element_get_static_pad(muxer_, ("sink_" + std::to_string(index)).c_str());
        GstElement* pad_signal_source =
            gst_bin_get_by_name(GST_BIN(source), make_source_element_name(camera.id).c_str());

        StreamSlot slot;
        slot.source_index = static_cast<uint32_t>(index);
        slot.camera_id = camera.id;
        slot.camera_uri = camera.uri;
        slot.is_seeded = true;
        slot.state = "active";
        slot.source = source;
        slot.mux_sink_pad = mux_sink_pad;
        slot.pad_signal_source = pad_signal_source;
        streams_[camera.id] = slot;

        max_existing_index = std::max(max_existing_index, static_cast<uint32_t>(index));
        found_existing = true;
    }

    next_source_index_ = found_existing ? (max_existing_index + 1U) : 0U;
}

}  // namespace engine::pipeline
