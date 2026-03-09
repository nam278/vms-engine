#pragma once

#include "engine/core/config/config_types.hpp"

#include <nvbufsurface.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::pipeline::evidence {

/** @brief Object snapshot stored alongside one emitted frame for later crop requests. */
struct FrameObjectSnapshot {
    std::string object_key;
    std::string instance_key;
    std::string crop_ref;
    uint64_t object_id = 0;
    uint64_t tracker_id = 0;
    int class_id = 0;
    std::string object_type;
    double confidence = 0.0;
    float left = 0.0F;
    float top = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    std::string parent_object_key;
    std::string parent_instance_key;
    int64_t parent_object_id = -1;
};

/** @brief Routing and frame metadata persisted for one emitted semantic frame. */
struct FrameCaptureMetadata {
    std::string schema_version;
    std::string pipeline_id;
    int source_id = 0;
    std::string source_name;
    uint64_t frame_num = 0;
    int64_t frame_ts_ms = 0;
    int64_t emitted_at_ms = 0;
    std::string frame_key;
    std::string overview_ref;
    int width = 0;
    int height = 0;
};

/**
 * @brief Owned cache entry for one emitted frame.
 *
 * `surface` is always engine-owned. Borrowed DeepStream pointers must never
 * escape the pad probe callback lifetime.
 */
struct CachedFrameEntry {
    FrameCaptureMetadata meta;
    std::vector<FrameObjectSnapshot> objects;
    NvBufSurface* surface = nullptr;
    int64_t cached_at_ms = 0;

    ~CachedFrameEntry();

    CachedFrameEntry() = default;
    CachedFrameEntry(const CachedFrameEntry&) = delete;
    CachedFrameEntry& operator=(const CachedFrameEntry&) = delete;
};

class FrameEvidenceCache {
   public:
    explicit FrameEvidenceCache(const engine::core::config::EvidenceConfig& config);
    ~FrameEvidenceCache();

    // Snapshot the emitted batch item so evidence requests can outlive the probe callback.
    bool store_frame(const FrameCaptureMetadata& meta,
                     const std::vector<FrameObjectSnapshot>& objects, NvBufSurface* batch_surface,
                     int batch_index);

    // Resolve by exact frame_key first, then nearest timestamp within max_frame_gap_ms.
    std::shared_ptr<const CachedFrameEntry> resolve(const std::string& pipeline_id,
                                                    const std::string& source_name, int source_id,
                                                    const std::string& frame_key,
                                                    int64_t frame_ts_ms) const;

    const FrameObjectSnapshot* find_object(const CachedFrameEntry& entry,
                                           const std::string& object_key,
                                           const std::string& instance_key,
                                           int64_t object_id) const;

    void clear();
    size_t size() const;

   private:
    std::string make_source_key(const std::string& pipeline_id, const std::string& source_name,
                                int source_id) const;
    void prune_locked(const std::string& source_key, int64_t now_ms) const;
    bool route_matches(const CachedFrameEntry& entry, const std::string& pipeline_id,
                       const std::string& source_name, int source_id) const;

    engine::core::config::EvidenceConfig config_;
    mutable std::mutex mutex_;
    mutable std::unordered_map<std::string, std::shared_ptr<CachedFrameEntry>> entries_;
    mutable std::unordered_map<std::string, std::deque<std::string>> source_entries_;
};

}  // namespace engine::pipeline::evidence