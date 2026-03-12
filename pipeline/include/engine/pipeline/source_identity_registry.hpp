#pragma once

#include <gst/gst.h>

#include <string>

namespace engine::pipeline {

void register_runtime_source_name(GstElement* source_root, int source_id,
                                  const std::string& source_name);

void unregister_runtime_source_name(GstElement* source_root, int source_id);

std::string lookup_runtime_source_name(GstElement* source_root, int source_id);

}  // namespace engine::pipeline