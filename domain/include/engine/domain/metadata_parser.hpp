#pragma once

#include "engine/domain/event_processor.hpp"
#include <any>
#include <string>
#include <vector>

namespace engine::domain {

/**
 * @brief Parses backend-specific metadata into domain types.
 *
 * The concrete DeepStream implementation (NvDsBatchMeta → FrameEvent)
 * lives in the pipeline layer. This interface decouples domain logic
 * from any specific backend SDK.
 */
class IMetadataParser {
   public:
    virtual ~IMetadataParser() = default;

    /**
     * @brief Parse batch metadata into a list of FrameEvents.
     * @param batch_meta Backend-specific batch metadata.
     * @return One FrameEvent per source in the batch.
     */
    virtual std::vector<FrameEvent> parse_batch(const std::any& batch_meta) = 0;

    /**
     * @brief Parse a single frame's object metadata into DetectionResults.
     * @param frame_meta Backend-specific per-frame metadata.
     * @return All detections in this frame.
     */
    virtual std::vector<DetectionResult> parse_frame_objects(const std::any& frame_meta) = 0;

    /**
     * @brief Extract source URI from frame metadata.
     * @param frame_meta Backend-specific per-frame metadata.
     * @param source_id  Source index.
     * @return URI string (e.g. rtsp://...).
     */
    virtual std::string get_source_uri(const std::any& frame_meta, int source_id) const = 0;
};

}  // namespace engine::domain
