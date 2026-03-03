#pragma once
#include <gst/gst.h>
#include <glib.h>
#include <memory>

namespace engine::core::utils {

/// @brief RAII guard for GstElement NOT yet added to a bin.
using GstElementPtr = std::unique_ptr<GstElement, decltype(&gst_object_unref)>;

inline GstElementPtr make_gst_element(const char* factory, const char* name) {
    return GstElementPtr(gst_element_factory_make(factory, name), gst_object_unref);
}

using GstCapsPtr = std::unique_ptr<GstCaps, decltype(&gst_caps_unref)>;
using GstPadPtr = std::unique_ptr<GstPad, decltype(&gst_object_unref)>;
using GstBusPtr = std::unique_ptr<GstBus, decltype(&gst_object_unref)>;
using GMainLoopPtr = std::unique_ptr<GMainLoop, decltype(&g_main_loop_unref)>;
using GErrorPtr = std::unique_ptr<GError, decltype(&g_error_free)>;
using GCharPtr = std::unique_ptr<gchar, decltype(&g_free)>;

}  // namespace engine::core::utils
