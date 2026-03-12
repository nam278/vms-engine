#include "engine/pipeline/runtime_stream_manager.hpp"
#include "engine/pipeline/builders/nvurisrcbin_builder.hpp"
#include "engine/pipeline/source_identity_registry.hpp"
#include "engine/pipeline/source_naming.hpp"
#include "engine/core/utils/gst_utils.hpp"
#include "engine/core/utils/logger.hpp"

#include <algorithm>
#include <cstring>

using engine::core::pipeline::RuntimeSourceErrorCode;
using engine::core::pipeline::RuntimeSourceInfo;
using engine::core::pipeline::RuntimeSourceMutationResult;

namespace engine::pipeline {

namespace {

struct StreamProbeContext {
    engine::pipeline::RuntimeStreamManager* manager = nullptr;
    uint32_t source_index = 0;
};

constexpr const char* kSlotBinPrefix = "source_slot_";
constexpr const char* kSlotSelectorPrefix = "source_slot_selector_";
constexpr const char* kSlotPlaceholderSrcPrefix = "source_slot_placeholder_src_";
constexpr const char* kSlotPlaceholderCapsPrefix = "source_slot_placeholder_caps_";
constexpr const char* kSlotPlaceholderConvertPrefix = "source_slot_placeholder_convert_";
constexpr const char* kSlotPlaceholderNvmmCapsPrefix = "source_slot_placeholder_nvmm_caps_";
constexpr int kPlaceholderWidth = 1920;
constexpr int kPlaceholderHeight = 1080;
constexpr int kPlaceholderFramerateNum = 25;
constexpr int kPlaceholderFramerateDen = 1;
constexpr gint64 kMinimumStallTimeoutUs = 1500 * G_TIME_SPAN_MILLISECOND;

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

gint64 stream_stall_timeout_us(const engine::core::config::SourcesConfig& sources_config) {
    const gint64 latency_timeout =
        static_cast<gint64>(std::max(1, sources_config.latency)) * 4 * G_TIME_SPAN_MILLISECOND;
    return std::max(kMinimumStallTimeoutUs, latency_timeout);
}

uint32_t max_slots_from_config(const engine::core::config::SourcesConfig& sources_config) {
    if (sources_config.mux.max_sources > 0) {
        return static_cast<uint32_t>(sources_config.mux.max_sources);
    }
    if (sources_config.mux.batch_size > 0) {
        return static_cast<uint32_t>(sources_config.mux.batch_size);
    }
    return static_cast<uint32_t>(std::max(1, sources_config.max_batch_size));
}

std::string make_slot_bin_name(uint32_t source_index) {
    return std::string(kSlotBinPrefix) + std::to_string(source_index);
}

std::string make_slot_selector_name(uint32_t source_index) {
    return std::string(kSlotSelectorPrefix) + std::to_string(source_index);
}

std::string make_slot_placeholder_src_name(uint32_t source_index) {
    return std::string(kSlotPlaceholderSrcPrefix) + std::to_string(source_index);
}

std::string make_slot_placeholder_caps_name(uint32_t source_index) {
    return std::string(kSlotPlaceholderCapsPrefix) + std::to_string(source_index);
}

std::string make_slot_placeholder_convert_name(uint32_t source_index) {
    return std::string(kSlotPlaceholderConvertPrefix) + std::to_string(source_index);
}

std::string make_slot_placeholder_nvmm_caps_name(uint32_t source_index) {
    return std::string(kSlotPlaceholderNvmmCapsPrefix) + std::to_string(source_index);
}

bool parse_slot_index_from_name(const std::string& element_name, uint32_t& source_index) {
    if (element_name.rfind(kSlotBinPrefix, 0) != 0) {
        return false;
    }

    try {
        source_index =
            static_cast<uint32_t>(std::stoul(element_name.substr(std::strlen(kSlotBinPrefix))));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool add_ghost_pad(GstElement* bin, const char* ghost_name, GstPad* target_pad) {
    if (bin == nullptr || ghost_name == nullptr || target_pad == nullptr) {
        return false;
    }

    GstPad* ghost_pad = gst_ghost_pad_new(ghost_name, target_pad);
    if (ghost_pad == nullptr) {
        return false;
    }

    gst_pad_set_active(ghost_pad, TRUE);
    if (!gst_element_add_pad(bin, ghost_pad)) {
        gst_object_unref(ghost_pad);
        return false;
    }

    return true;
}

std::string make_placeholder_caps_string(bool nvmm, int width, int height) {
    std::string caps = nvmm ? "video/x-raw(memory:NVMM),format=NV12" : "video/x-raw,format=NV12";
    caps += ",width=" + std::to_string(width);
    caps += ",height=" + std::to_string(height);
    caps += ",framerate=" + std::to_string(kPlaceholderFramerateNum) + "/" +
            std::to_string(kPlaceholderFramerateDen);
    return caps;
}

GstPad* find_element_pad_by_name(GstElement* element, GstPadDirection direction,
                                 const std::string& pad_name) {
    if (element == nullptr) {
        return nullptr;
    }

    GstIterator* iterator = direction == GST_PAD_SRC ? gst_element_iterate_src_pads(element)
                                                     : gst_element_iterate_sink_pads(element);
    if (iterator == nullptr) {
        return nullptr;
    }

    GstPad* matched_pad = nullptr;
    GValue value = G_VALUE_INIT;
    bool done = false;

    while (!done) {
        switch (gst_iterator_next(iterator, &value)) {
            case GST_ITERATOR_OK: {
                auto* pad = GST_PAD(g_value_get_object(&value));
                if (pad != nullptr && std::strcmp(GST_PAD_NAME(pad), pad_name.c_str()) == 0) {
                    matched_pad = GST_PAD(gst_object_ref(pad));
                    done = true;
                }
                g_value_reset(&value);
                break;
            }
            case GST_ITERATOR_RESYNC:
                gst_iterator_resync(iterator);
                g_value_reset(&value);
                break;
            default:
                done = true;
                break;
        }
    }

    g_value_unset(&value);
    gst_iterator_free(iterator);
    return matched_pad;
}

void destroy_stream_probe_context(gpointer data) {
    delete static_cast<StreamProbeContext*>(data);
}

}  // namespace

RuntimeStreamManager::RuntimeStreamManager(GstElement* source_root, GstElement* muxer,
                                           engine::core::config::SourcesConfig sources_config)
    : source_root_(source_root != nullptr ? GST_ELEMENT(gst_object_ref(source_root)) : nullptr),
      muxer_(muxer != nullptr ? GST_ELEMENT(gst_object_ref(muxer)) : nullptr),
      sources_config_(std::move(sources_config)) {
    ensure_fixed_slots();
    seed_existing_streams();
    health_check_source_id_ =
        g_timeout_add(500, RuntimeStreamManager::on_stream_health_check, this);
}

RuntimeStreamManager::~RuntimeStreamManager() {
    if (health_check_source_id_ != 0) {
        g_source_remove(health_check_source_id_);
        health_check_source_id_ = 0;
    }

    for (auto& [camera_id, slot] : streams_) {
        detach_stream_probe(slot);
        if (slot.source != nullptr) {
            gst_object_unref(slot.source);
        }
    }
    streams_.clear();

    for (auto& [source_index, slot] : fixed_slots_) {
        if (slot.slot_bin != nullptr) {
            gst_object_unref(slot.slot_bin);
        }
        if (slot.selector != nullptr) {
            gst_object_unref(slot.selector);
        }
        if (slot.placeholder_sink_pad != nullptr) {
            gst_object_unref(slot.placeholder_sink_pad);
        }
        if (slot.live_sink_pad != nullptr) {
            gst_object_unref(slot.live_sink_pad);
        }
        if (slot.mux_sink_pad != nullptr) {
            gst_object_unref(slot.mux_sink_pad);
        }
    }
    fixed_slots_.clear();

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

bool RuntimeStreamManager::ensure_fixed_slots() {
    if (source_root_ == nullptr || muxer_ == nullptr) {
        return false;
    }

    const uint32_t max_slots = max_slots_from_config(sources_config_);
    for (uint32_t source_index = 0; source_index < max_slots; ++source_index) {
        if (fixed_slots_.find(source_index) != fixed_slots_.end()) {
            continue;
        }

        if (!discover_fixed_slot(source_index) && !create_fixed_slot(source_index)) {
            LOG_E("RuntimeStreamManager: failed to prepare fixed slot {}", source_index);
            return false;
        }
    }

    next_source_index_ = max_slots;
    return true;
}

bool RuntimeStreamManager::create_fixed_slot(uint32_t source_index) {
    auto slot_bin = engine::core::utils::GstElementPtr(
        gst_bin_new(make_slot_bin_name(source_index).c_str()), gst_object_unref);
    auto selector = engine::core::utils::make_gst_element(
        "input-selector", make_slot_selector_name(source_index).c_str());
    auto placeholder_src = engine::core::utils::make_gst_element(
        "videotestsrc", make_slot_placeholder_src_name(source_index).c_str());
    auto placeholder_caps = engine::core::utils::make_gst_element(
        "capsfilter", make_slot_placeholder_caps_name(source_index).c_str());
    auto placeholder_convert = engine::core::utils::make_gst_element(
        "nvvideoconvert", make_slot_placeholder_convert_name(source_index).c_str());
    auto placeholder_nvmm_caps = engine::core::utils::make_gst_element(
        "capsfilter", make_slot_placeholder_nvmm_caps_name(source_index).c_str());

    if (!slot_bin || !selector || !placeholder_src || !placeholder_caps || !placeholder_convert ||
        !placeholder_nvmm_caps) {
        LOG_E("RuntimeStreamManager: failed to create fixed-slot elements for source {}",
              source_index);
        return false;
    }

    const int width = sources_config_.width > 0 ? sources_config_.width : kPlaceholderWidth;
    const int height = sources_config_.height > 0 ? sources_config_.height : kPlaceholderHeight;

    gst_util_set_object_arg(G_OBJECT(placeholder_src.get()), "pattern", "black");
    g_object_set(G_OBJECT(placeholder_src.get()), "is-live", static_cast<gboolean>(TRUE),
                 "do-timestamp", static_cast<gboolean>(TRUE), nullptr);
    g_object_set(G_OBJECT(placeholder_convert.get()), "gpu-id",
                 static_cast<gint>(sources_config_.gpu_id), nullptr);

    engine::core::utils::GstCapsPtr system_caps(
        gst_caps_from_string(make_placeholder_caps_string(false, width, height).c_str()),
        gst_caps_unref);
    engine::core::utils::GstCapsPtr nvmm_caps(
        gst_caps_from_string(make_placeholder_caps_string(true, width, height).c_str()),
        gst_caps_unref);
    if (!system_caps || !nvmm_caps) {
        LOG_E("RuntimeStreamManager: failed to create placeholder caps for source {}",
              source_index);
        return false;
    }

    g_object_set(G_OBJECT(placeholder_caps.get()), "caps", system_caps.get(), nullptr);
    g_object_set(G_OBJECT(placeholder_nvmm_caps.get()), "caps", nvmm_caps.get(), nullptr);

    gst_bin_add_many(GST_BIN(slot_bin.get()), selector.get(), placeholder_src.get(),
                     placeholder_caps.get(), placeholder_convert.get(), placeholder_nvmm_caps.get(),
                     nullptr);

    selector.release();
    placeholder_src.release();
    placeholder_caps.release();
    placeholder_convert.release();
    placeholder_nvmm_caps.release();

    GstElement* slot_selector =
        gst_bin_get_by_name(GST_BIN(slot_bin.get()), make_slot_selector_name(source_index).c_str());
    GstElement* slot_placeholder_nvmm_caps = gst_bin_get_by_name(
        GST_BIN(slot_bin.get()), make_slot_placeholder_nvmm_caps_name(source_index).c_str());
    if (slot_selector == nullptr || slot_placeholder_nvmm_caps == nullptr) {
        if (slot_selector != nullptr) {
            gst_object_unref(slot_selector);
        }
        if (slot_placeholder_nvmm_caps != nullptr) {
            gst_object_unref(slot_placeholder_nvmm_caps);
        }
        LOG_E("RuntimeStreamManager: failed to recover fixed-slot elements for source {}",
              source_index);
        return false;
    }

    GstPad* placeholder_sink_pad = gst_element_request_pad_simple(slot_selector, "sink_%u");
    GstPad* live_sink_pad = gst_element_request_pad_simple(slot_selector, "sink_%u");
    if (placeholder_sink_pad == nullptr || live_sink_pad == nullptr) {
        if (placeholder_sink_pad != nullptr) {
            gst_object_unref(placeholder_sink_pad);
        }
        if (live_sink_pad != nullptr) {
            gst_object_unref(live_sink_pad);
        }
        gst_object_unref(slot_selector);
        gst_object_unref(slot_placeholder_nvmm_caps);
        LOG_E("RuntimeStreamManager: failed to request selector pads for slot {}", source_index);
        return false;
    }

    GstElement* slot_placeholder_src = gst_bin_get_by_name(
        GST_BIN(slot_bin.get()), make_slot_placeholder_src_name(source_index).c_str());
    GstElement* slot_placeholder_caps = gst_bin_get_by_name(
        GST_BIN(slot_bin.get()), make_slot_placeholder_caps_name(source_index).c_str());
    GstElement* slot_placeholder_convert = gst_bin_get_by_name(
        GST_BIN(slot_bin.get()), make_slot_placeholder_convert_name(source_index).c_str());
    if (slot_placeholder_src == nullptr || slot_placeholder_caps == nullptr ||
        slot_placeholder_convert == nullptr) {
        if (slot_placeholder_src != nullptr) {
            gst_object_unref(slot_placeholder_src);
        }
        if (slot_placeholder_caps != nullptr) {
            gst_object_unref(slot_placeholder_caps);
        }
        if (slot_placeholder_convert != nullptr) {
            gst_object_unref(slot_placeholder_convert);
        }
        gst_object_unref(placeholder_sink_pad);
        gst_object_unref(live_sink_pad);
        gst_object_unref(slot_selector);
        gst_object_unref(slot_placeholder_nvmm_caps);
        LOG_E("RuntimeStreamManager: failed to recover placeholder chain for slot {}",
              source_index);
        return false;
    }

    if (!gst_element_link_many(slot_placeholder_src, slot_placeholder_caps,
                               slot_placeholder_convert, slot_placeholder_nvmm_caps, nullptr)) {
        gst_object_unref(slot_placeholder_src);
        gst_object_unref(slot_placeholder_caps);
        gst_object_unref(slot_placeholder_convert);
        LOG_E("RuntimeStreamManager: failed to link placeholder chain for slot {}", source_index);
        return false;
    }

    engine::core::utils::GstPadPtr placeholder_src_pad(
        gst_element_get_static_pad(slot_placeholder_nvmm_caps, "src"), gst_object_unref);
    gst_object_unref(slot_placeholder_src);
    gst_object_unref(slot_placeholder_caps);
    gst_object_unref(slot_placeholder_convert);
    gst_object_unref(slot_placeholder_nvmm_caps);
    if (!placeholder_src_pad ||
        gst_pad_link(placeholder_src_pad.get(), placeholder_sink_pad) != GST_PAD_LINK_OK) {
        gst_object_unref(placeholder_sink_pad);
        gst_object_unref(live_sink_pad);
        gst_object_unref(slot_selector);
        LOG_E("RuntimeStreamManager: failed to connect placeholder output to selector for slot {}",
              source_index);
        return false;
    }

    engine::core::utils::GstPadPtr selector_src_pad(
        gst_element_get_static_pad(slot_selector, "src"), gst_object_unref);
    if (!selector_src_pad || !add_ghost_pad(slot_bin.get(), "src", selector_src_pad.get()) ||
        !add_ghost_pad(slot_bin.get(), "sink", live_sink_pad)) {
        gst_object_unref(placeholder_sink_pad);
        gst_object_unref(live_sink_pad);
        gst_object_unref(slot_selector);
        LOG_E("RuntimeStreamManager: failed to expose fixed-slot ghost pads for slot {}",
              source_index);
        return false;
    }

    if (!gst_bin_add(GST_BIN(source_root_), slot_bin.get())) {
        gst_object_unref(placeholder_sink_pad);
        gst_object_unref(live_sink_pad);
        gst_object_unref(slot_selector);
        LOG_E("RuntimeStreamManager: failed to add fixed slot {} to source root", source_index);
        return false;
    }

    GstPad* mux_sink_pad =
        gst_element_request_pad_simple(muxer_, ("sink_" + std::to_string(source_index)).c_str());
    if (mux_sink_pad == nullptr) {
        gst_object_unref(placeholder_sink_pad);
        gst_object_unref(live_sink_pad);
        gst_object_unref(slot_selector);
        LOG_E("RuntimeStreamManager: failed to request mux sink pad for slot {}", source_index);
        return false;
    }

    engine::core::utils::GstPadPtr slot_src_pad(gst_element_get_static_pad(slot_bin.get(), "src"),
                                                gst_object_unref);
    if (!slot_src_pad || gst_pad_link(slot_src_pad.get(), mux_sink_pad) != GST_PAD_LINK_OK) {
        gst_object_unref(placeholder_sink_pad);
        gst_object_unref(live_sink_pad);
        gst_object_unref(mux_sink_pad);
        gst_object_unref(slot_selector);
        LOG_E("RuntimeStreamManager: failed to link fixed slot {} to nvstreammux", source_index);
        return false;
    }

    if (GST_OBJECT_PARENT(source_root_) != nullptr) {
        gst_element_sync_state_with_parent(slot_bin.get());
    }

    g_object_set(G_OBJECT(slot_selector), "active-pad", placeholder_sink_pad, nullptr);

    FixedSlot slot;
    slot.source_index = source_index;
    slot.slot_bin = GST_ELEMENT(gst_object_ref(slot_bin.get()));
    slot.selector = slot_selector;
    slot.placeholder_sink_pad = placeholder_sink_pad;
    slot.live_sink_pad = live_sink_pad;
    slot.mux_sink_pad = mux_sink_pad;
    fixed_slots_.emplace(source_index, slot);

    LOG_I("RuntimeStreamManager: created fixed slot {} with stable mux pad sink_{}", source_index,
          source_index);
    return true;
}

bool RuntimeStreamManager::discover_fixed_slot(uint32_t source_index) {
    GstElement* slot_bin =
        gst_bin_get_by_name(GST_BIN(source_root_), make_slot_bin_name(source_index).c_str());
    if (slot_bin == nullptr) {
        return false;
    }

    GstElement* selector =
        gst_bin_get_by_name(GST_BIN(slot_bin), make_slot_selector_name(source_index).c_str());
    GstPad* placeholder_sink_pad =
        selector != nullptr ? find_element_pad_by_name(selector, GST_PAD_SINK, "sink_0") : nullptr;
    GstPad* live_sink_pad =
        selector != nullptr ? find_element_pad_by_name(selector, GST_PAD_SINK, "sink_1") : nullptr;
    GstPad* mux_sink_pad =
        find_element_pad_by_name(muxer_, GST_PAD_SINK, "sink_" + std::to_string(source_index));

    if (selector == nullptr || placeholder_sink_pad == nullptr || live_sink_pad == nullptr ||
        mux_sink_pad == nullptr) {
        if (slot_bin != nullptr) {
            gst_object_unref(slot_bin);
        }
        if (selector != nullptr) {
            gst_object_unref(selector);
        }
        if (placeholder_sink_pad != nullptr) {
            gst_object_unref(placeholder_sink_pad);
        }
        if (live_sink_pad != nullptr) {
            gst_object_unref(live_sink_pad);
        }
        if (mux_sink_pad != nullptr) {
            gst_object_unref(mux_sink_pad);
        }
        return false;
    }

    FixedSlot slot;
    slot.source_index = source_index;
    slot.slot_bin = slot_bin;
    slot.selector = selector;
    slot.placeholder_sink_pad = placeholder_sink_pad;
    slot.live_sink_pad = live_sink_pad;
    slot.mux_sink_pad = mux_sink_pad;
    fixed_slots_.emplace(source_index, slot);
    return true;
}

bool RuntimeStreamManager::switch_slot_to_live(uint32_t source_index) {
    const auto it = fixed_slots_.find(source_index);
    if (it == fixed_slots_.end() || it->second.selector == nullptr ||
        it->second.live_sink_pad == nullptr) {
        return false;
    }

    g_object_set(G_OBJECT(it->second.selector), "active-pad", it->second.live_sink_pad, nullptr);
    return true;
}

bool RuntimeStreamManager::switch_slot_to_placeholder(uint32_t source_index) {
    const auto it = fixed_slots_.find(source_index);
    if (it == fixed_slots_.end() || it->second.selector == nullptr ||
        it->second.placeholder_sink_pad == nullptr) {
        return false;
    }

    g_object_set(G_OBJECT(it->second.selector), "active-pad", it->second.placeholder_sink_pad,
                 nullptr);
    return true;
}

bool RuntimeStreamManager::switch_slot_to_idle(uint32_t source_index) {
    const auto it = fixed_slots_.find(source_index);
    if (it == fixed_slots_.end() || it->second.selector == nullptr) {
        return false;
    }

    g_object_set(G_OBJECT(it->second.selector), "active-pad", nullptr, nullptr);
    return true;
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
    const auto fixed_slot_it = fixed_slots_.find(source_index);
    if (fixed_slot_it == fixed_slots_.end() || fixed_slot_it->second.slot_bin == nullptr) {
        release_source_index(source_index);
        LOG_E("RuntimeStreamManager: fixed slot {} is unavailable for '{}'", source_index,
              camera.id);
        return make_result(false, 500, RuntimeSourceErrorCode::InternalError, camera.id,
                           "fixed source slot is unavailable");
    }

    const std::string source_bin_name = make_source_bin_name(camera.id);

    builders::NvUriSrcBinBuilder source_builder(source_root_);
    GstElement* source = source_builder.build(sources_config_, camera, source_index);
    if (source == nullptr) {
        release_source_index(source_index);
        LOG_E("RuntimeStreamManager: failed to build source bin '{}'", source_bin_name);
        return make_result(false, 500, RuntimeSourceErrorCode::BuildSourceFailed, camera.id,
                           "failed to build source bin");
    }

    const auto cleanup_failed_add = [&](RuntimeSourceErrorCode error_code,
                                        const std::string& message) {
        gst_element_unlink(source, fixed_slot_it->second.slot_bin);
        if (source != nullptr && GST_OBJECT_PARENT(source) == GST_OBJECT(source_root_)) {
            gst_bin_remove(GST_BIN(source_root_), source);
        }
        release_source_index(source_index);
        return make_result(false, 500, error_code, camera.id, message);
    };

    if (!gst_element_link(source, fixed_slot_it->second.slot_bin)) {
        LOG_E("RuntimeStreamManager: failed to link source '{}' into fixed slot {}", camera.id,
              source_index);
        return cleanup_failed_add(RuntimeSourceErrorCode::LinkSourceFailed,
                                  "failed to link source bin into fixed slot");
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
    slot.state = "warming_up";
    slot.source = GST_ELEMENT(gst_object_ref(source));
    slot.last_buffer_time_us = g_get_monotonic_time();
    slot.using_placeholder = true;
    attach_stream_probe(slot);

    if (!switch_slot_to_placeholder(source_index)) {
        detach_stream_probe(slot);
        gst_object_unref(slot.source);
        slot.source = nullptr;
        LOG_E("RuntimeStreamManager: failed to activate placeholder pad for slot {}", source_index);
        return cleanup_failed_add(RuntimeSourceErrorCode::InternalError,
                                  "failed to activate placeholder source slot");
    }

    streams_.emplace(camera.id, slot);
    register_runtime_source_name(source_root_, static_cast<int>(source_index), camera.id);
    LOG_I(
        "RuntimeStreamManager: added camera '{}' into fixed slot {} (uri='{}'), waiting for first "
        "buffer before switching live",
        camera.id, source_index, camera.uri);

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
    const auto fixed_slot_it = fixed_slots_.find(slot.source_index);
    if (fixed_slot_it == fixed_slots_.end()) {
        return make_result(false, 500, RuntimeSourceErrorCode::InternalError, camera_id,
                           "fixed source slot is unavailable");
    }

    if (!switch_slot_to_placeholder(slot.source_index)) {
        LOG_W("RuntimeStreamManager: failed to switch slot {} to placeholder", slot.source_index);
    }

    detach_stream_probe(slot);

    if (slot.source != nullptr) {
        gst_element_unlink(slot.source, fixed_slot_it->second.slot_bin);

        gst_element_set_state(slot.source, GST_STATE_READY);
        if (!wait_for_element_state(slot.source, GST_STATE_READY, 5 * GST_SECOND)) {
            LOG_W("RuntimeStreamManager: source '{}' did not settle in READY within timeout",
                  camera_id);
        }
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

    unregister_runtime_source_name(source_root_, static_cast<int>(slot.source_index));
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

RuntimeStreamManager::StreamSlot* RuntimeStreamManager::find_stream_by_source_index_locked(
    uint32_t source_index) {
    for (auto& [camera_id, slot] : streams_) {
        if (slot.source_index == source_index) {
            return &slot;
        }
    }

    return nullptr;
}

void RuntimeStreamManager::attach_stream_probe(StreamSlot& slot) {
    if (slot.source == nullptr || slot.source_src_pad != nullptr || slot.buffer_probe_id != 0) {
        return;
    }

    GstPad* source_src_pad = gst_element_get_static_pad(slot.source, "src");
    if (source_src_pad == nullptr) {
        LOG_W("RuntimeStreamManager: source '{}' has no static src pad for activity probe",
              slot.camera_id);
        return;
    }

    auto* context = new StreamProbeContext{this, slot.source_index};
    const gulong probe_id = gst_pad_add_probe(source_src_pad, GST_PAD_PROBE_TYPE_BUFFER,
                                              RuntimeStreamManager::on_stream_buffer_probe, context,
                                              destroy_stream_probe_context);
    if (probe_id == 0) {
        gst_object_unref(source_src_pad);
        delete context;
        LOG_W("RuntimeStreamManager: failed to attach activity probe for '{}'", slot.camera_id);
        return;
    }

    slot.source_src_pad = source_src_pad;
    slot.buffer_probe_id = probe_id;
}

GstPadProbeReturn RuntimeStreamManager::on_stream_buffer_probe(GstPad* /*pad*/,
                                                               GstPadProbeInfo* info,
                                                               gpointer user_data) {
    if ((GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
        return GST_PAD_PROBE_OK;
    }

    auto* context = static_cast<StreamProbeContext*>(user_data);
    if (context != nullptr && context->manager != nullptr) {
        context->manager->note_stream_activity(context->source_index);
    }

    return GST_PAD_PROBE_OK;
}

gboolean RuntimeStreamManager::on_stream_health_check(gpointer data) {
    auto* manager = static_cast<RuntimeStreamManager*>(data);
    if (manager == nullptr) {
        return G_SOURCE_REMOVE;
    }

    return manager->poll_stream_health();
}

void RuntimeStreamManager::detach_stream_probe(StreamSlot& slot) {
    if (slot.source_src_pad != nullptr && slot.buffer_probe_id != 0) {
        gst_pad_remove_probe(slot.source_src_pad, slot.buffer_probe_id);
        slot.buffer_probe_id = 0;
    }

    if (slot.source_src_pad != nullptr) {
        gst_object_unref(slot.source_src_pad);
        slot.source_src_pad = nullptr;
    }
}

void RuntimeStreamManager::note_stream_activity(uint32_t source_index) {
    std::lock_guard<std::mutex> lock(mtx_);

    StreamSlot* slot = find_stream_by_source_index_locked(source_index);
    if (slot == nullptr) {
        return;
    }

    slot->last_buffer_time_us = g_get_monotonic_time();
    if (slot->using_placeholder) {
        if (switch_slot_to_live(source_index)) {
            slot->using_placeholder = false;
            slot->state = "active";
            LOG_I("RuntimeStreamManager: restored camera '{}' to live after buffer activity",
                  slot->camera_id);
        }
    }
}

gboolean RuntimeStreamManager::poll_stream_health() {
    std::lock_guard<std::mutex> lock(mtx_);

    const gint64 now_us = g_get_monotonic_time();
    const gint64 stale_after_us = stream_stall_timeout_us(sources_config_);

    for (auto& [camera_id, slot] : streams_) {
        if (slot.source == nullptr || slot.using_placeholder) {
            continue;
        }

        if (slot.last_buffer_time_us <= 0) {
            continue;
        }

        if (now_us - slot.last_buffer_time_us < stale_after_us) {
            continue;
        }

        if (switch_slot_to_placeholder(slot.source_index)) {
            slot.using_placeholder = true;
            slot.state = "recovering";
            LOG_W(
                "RuntimeStreamManager: camera '{}' stalled for {} ms, switching slot {} to "
                "placeholder until frames resume",
                camera_id, (now_us - slot.last_buffer_time_us) / G_TIME_SPAN_MILLISECOND,
                slot.source_index);
        }
    }

    return G_SOURCE_CONTINUE;
}

uint32_t RuntimeStreamManager::allocate_source_index() {
    if (!free_source_indexes_.empty()) {
        const uint32_t source_index = free_source_indexes_.back();
        free_source_indexes_.pop_back();
        return source_index;
    }

    if (next_source_index_ < max_slots_from_config(sources_config_)) {
        return next_source_index_++;
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

    const uint32_t max_slots = max_slots_from_config(sources_config_);
    std::vector<bool> occupied(max_slots, false);
    streams_.clear();

    for (const auto& camera : sources_config_.cameras) {
        if (camera.id.empty()) {
            continue;
        }

        GstElement* source =
            gst_bin_get_by_name(GST_BIN(source_root_), make_source_bin_name(camera.id).c_str());
        if (source == nullptr) {
            continue;
        }

        uint32_t source_index = 0;
        bool resolved_slot = false;
        engine::core::utils::GstPadPtr source_src_pad(gst_element_get_static_pad(source, "src"),
                                                      gst_object_unref);
        if (source_src_pad) {
            GstPad* peer_pad = gst_pad_get_peer(source_src_pad.get());
            if (peer_pad != nullptr) {
                GstObject* parent = gst_pad_get_parent(peer_pad);
                if (parent != nullptr && GST_IS_ELEMENT(parent)) {
                    resolved_slot =
                        parse_slot_index_from_name(GST_ELEMENT_NAME(parent), source_index);
                }
                if (parent != nullptr) {
                    gst_object_unref(parent);
                }
                gst_object_unref(peer_pad);
            }
        }

        if (!resolved_slot || source_index >= max_slots) {
            gst_object_unref(source);
            continue;
        }

        switch_slot_to_live(source_index);

        StreamSlot slot;
        slot.source_index = source_index;
        slot.camera_id = camera.id;
        slot.camera_uri = camera.uri;
        slot.is_seeded = true;
        slot.state = "active";
        slot.source = source;
        slot.last_buffer_time_us = g_get_monotonic_time();
        slot.using_placeholder = false;
        attach_stream_probe(slot);
        streams_[camera.id] = slot;
        register_runtime_source_name(source_root_, static_cast<int>(source_index), camera.id);
        occupied[source_index] = true;
    }

    free_source_indexes_.clear();
    for (uint32_t source_index = 0; source_index < max_slots; ++source_index) {
        if (!occupied[source_index]) {
            switch_slot_to_idle(source_index);
            unregister_runtime_source_name(source_root_, static_cast<int>(source_index));
            free_source_indexes_.push_back(source_index);
        }
    }
    std::sort(free_source_indexes_.begin(), free_source_indexes_.end(), std::greater<uint32_t>());
    next_source_index_ = max_slots;
}

}  // namespace engine::pipeline
