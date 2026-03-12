#include "engine/pipeline/source_identity_registry.hpp"

#include <glib-object.h>

#include <mutex>
#include <unordered_map>

namespace engine::pipeline {

namespace {

constexpr const char* kSourceIdentityRegistryDataKey = "engine-source-identity-registry";

struct SourceIdentityRegistry {
    std::mutex mutex;
    std::unordered_map<int, std::string> source_id_to_name;
};

void destroy_source_identity_registry(gpointer data) {
    delete static_cast<SourceIdentityRegistry*>(data);
}

SourceIdentityRegistry* get_registry(GstElement* source_root) {
    if (source_root == nullptr) {
        return nullptr;
    }

    return static_cast<SourceIdentityRegistry*>(
        g_object_get_data(G_OBJECT(source_root), kSourceIdentityRegistryDataKey));
}

SourceIdentityRegistry* get_or_create_registry(GstElement* source_root) {
    if (source_root == nullptr) {
        return nullptr;
    }

    auto* registry = get_registry(source_root);
    if (registry != nullptr) {
        return registry;
    }

    registry = new SourceIdentityRegistry();
    g_object_set_data_full(G_OBJECT(source_root), kSourceIdentityRegistryDataKey, registry,
                           destroy_source_identity_registry);
    return registry;
}

}  // namespace

void register_runtime_source_name(GstElement* source_root, int source_id,
                                  const std::string& source_name) {
    auto* registry = get_or_create_registry(source_root);
    if (registry == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(registry->mutex);
    registry->source_id_to_name[source_id] = source_name;
}

void unregister_runtime_source_name(GstElement* source_root, int source_id) {
    auto* registry = get_registry(source_root);
    if (registry == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(registry->mutex);
    registry->source_id_to_name.erase(source_id);
}

std::string lookup_runtime_source_name(GstElement* source_root, int source_id) {
    auto* registry = get_registry(source_root);
    if (registry == nullptr) {
        return {};
    }

    std::lock_guard<std::mutex> lock(registry->mutex);
    const auto it = registry->source_id_to_name.find(source_id);
    return it != registry->source_id_to_name.end() ? it->second : std::string();
}

}  // namespace engine::pipeline