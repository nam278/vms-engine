#include "engine/pipeline/evidence/frame_evidence_cache.hpp"

#include "engine/core/utils/logger.hpp"

#include <chrono>
#include <cmath>

namespace engine::pipeline::evidence {

namespace {

int64_t now_epoch_ms() {
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

bool clone_surface_batch_item(NvBufSurface* batch_surface, int batch_index,
                              NvBufSurface** out_surface) {
    if (!batch_surface || !out_surface || batch_index < 0 ||
        batch_index >= static_cast<int>(batch_surface->numFilled)) {
        return false;
    }

    const NvBufSurfaceParams& source_params = batch_surface->surfaceList[batch_index];

    NvBufSurfaceCreateParams create_params{};
    create_params.gpuId = batch_surface->gpuId;
    create_params.width = source_params.width;
    create_params.height = source_params.height;
    create_params.size = 0;
    create_params.isContiguous = false;
    create_params.colorFormat = source_params.colorFormat;
    create_params.layout = source_params.layout;
    create_params.memType = batch_surface->memType;

    NvBufSurface* destination = nullptr;
    if (NvBufSurfaceCreate(&destination, 1, &create_params) != 0 || !destination) {
        LOG_E("FrameEvidenceCache: NvBufSurfaceCreate failed for snapshot clone");
        return false;
    }

    // Copy only the emitted batch item into a single-surface view so the cache owns
    // an isolated snapshot and never depends on the original batched buffer lifetime.
    NvBufSurface source_view{};
    source_view.gpuId = batch_surface->gpuId;
    source_view.batchSize = 1;
    source_view.numFilled = 1;
    source_view.isContiguous = false;
    source_view.memType = batch_surface->memType;
    source_view.surfaceList = &batch_surface->surfaceList[batch_index];
    source_view.isImportedBuf = batch_surface->isImportedBuf;

    if (NvBufSurfaceCopy(&source_view, destination) != 0) {
        LOG_E("FrameEvidenceCache: NvBufSurfaceCopy failed for snapshot clone");
        NvBufSurfaceDestroy(destination);
        return false;
    }

    destination->numFilled = 1;
    *out_surface = destination;
    return true;
}

}  // namespace

CachedFrameEntry::~CachedFrameEntry() {
    if (surface) {
        NvBufSurfaceDestroy(surface);
        surface = nullptr;
    }
}

FrameEvidenceCache::FrameEvidenceCache(const engine::core::config::EvidenceConfig& config)
    : config_(config) {}

FrameEvidenceCache::~FrameEvidenceCache() {
    clear();
}

bool FrameEvidenceCache::store_frame(const FrameCaptureMetadata& meta,
                                     const std::vector<FrameObjectSnapshot>& objects,
                                     NvBufSurface* batch_surface, int batch_index) {
    if (!config_.cache_on_frame_events) {
        return false;
    }

    auto entry = std::make_shared<CachedFrameEntry>();
    entry->meta = meta;
    entry->objects = objects;
    entry->cached_at_ms = now_epoch_ms();

    if (!clone_surface_batch_item(batch_surface, batch_index, &entry->surface)) {
        return false;
    }

    const std::string source_key =
        make_source_key(meta.pipeline_id, meta.source_name, meta.source_id);

    std::lock_guard<std::mutex> lk(mutex_);
    entries_[meta.frame_key] = entry;
    auto& ordered_keys = source_entries_[source_key];
    ordered_keys.push_back(meta.frame_key);
    prune_locked(source_key, entry->cached_at_ms);

    return true;
}

std::shared_ptr<const CachedFrameEntry> FrameEvidenceCache::resolve(const std::string& pipeline_id,
                                                                    const std::string& source_name,
                                                                    int source_id,
                                                                    const std::string& frame_key,
                                                                    int64_t frame_ts_ms) const {
    const std::string source_key = make_source_key(pipeline_id, source_name, source_id);
    const int64_t now_ms = now_epoch_ms();

    std::lock_guard<std::mutex> lk(mutex_);
    prune_locked(source_key, now_ms);

    auto exact_it = entries_.find(frame_key);
    if (exact_it != entries_.end() &&
        route_matches(*exact_it->second, pipeline_id, source_name, source_id)) {
        return exact_it->second;
    }

    auto source_it = source_entries_.find(source_key);
    if (source_it == source_entries_.end()) {
        return nullptr;
    }

    // Fallback is only for small timestamp drift between the request and the emitted frame.
    std::shared_ptr<CachedFrameEntry> best_match;
    int64_t best_delta = static_cast<int64_t>(config_.max_frame_gap_ms) + 1;
    for (const auto& candidate_key : source_it->second) {
        auto entry_it = entries_.find(candidate_key);
        if (entry_it == entries_.end()) {
            continue;
        }

        const auto& candidate = entry_it->second;
        if (!route_matches(*candidate, pipeline_id, source_name, source_id)) {
            continue;
        }

        const int64_t delta = std::llabs(candidate->meta.frame_ts_ms - frame_ts_ms);
        if (delta <= config_.max_frame_gap_ms && delta < best_delta) {
            best_delta = delta;
            best_match = candidate;
        }
    }

    return best_match;
}

const FrameObjectSnapshot* FrameEvidenceCache::find_object(const CachedFrameEntry& entry,
                                                           const std::string& object_key,
                                                           const std::string& instance_key,
                                                           int64_t object_id) const {
    for (const auto& object : entry.objects) {
        if (!object_key.empty() && object.object_key == object_key) {
            return &object;
        }
        if (!instance_key.empty() && object.instance_key == instance_key) {
            return &object;
        }
        if (object_id >= 0 && object.object_id == static_cast<uint64_t>(object_id)) {
            return &object;
        }
    }
    return nullptr;
}

void FrameEvidenceCache::clear() {
    std::lock_guard<std::mutex> lk(mutex_);
    entries_.clear();
    source_entries_.clear();
}

size_t FrameEvidenceCache::size() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return entries_.size();
}

std::string FrameEvidenceCache::make_source_key(const std::string& pipeline_id,
                                                const std::string& source_name,
                                                int source_id) const {
    return pipeline_id + "|" + source_name + "|" + std::to_string(source_id);
}

void FrameEvidenceCache::prune_locked(const std::string& source_key, int64_t now_ms) const {
    auto source_it = source_entries_.find(source_key);
    if (source_it == source_entries_.end()) {
        return;
    }

    auto& ordered_keys = source_it->second;
    while (!ordered_keys.empty()) {
        auto entry_it = entries_.find(ordered_keys.front());
        if (entry_it == entries_.end()) {
            ordered_keys.pop_front();
            continue;
        }

        const bool expired = (now_ms - entry_it->second->cached_at_ms) > config_.frame_cache_ttl_ms;
        const bool over_limit =
            config_.max_frames_per_source > 0 &&
            ordered_keys.size() > static_cast<size_t>(config_.max_frames_per_source);

        if (!expired && !over_limit) {
            break;
        }

        entries_.erase(entry_it);
        ordered_keys.pop_front();
    }

    if (ordered_keys.empty()) {
        source_entries_.erase(source_it);
    }
}

bool FrameEvidenceCache::route_matches(const CachedFrameEntry& entry,
                                       const std::string& pipeline_id,
                                       const std::string& source_name, int source_id) const {
    return entry.meta.pipeline_id == pipeline_id && entry.meta.source_name == source_name &&
           entry.meta.source_id == source_id;
}

}  // namespace engine::pipeline::evidence