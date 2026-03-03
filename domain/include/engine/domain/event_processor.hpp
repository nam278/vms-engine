#pragma once

#include <any>
#include <string>
#include <vector>

namespace engine::domain {

/**
 * @brief A single detection from a video frame.
 *
 * Framework-agnostic — populated by IMetadataParser implementations
 * (DeepStream-specific parsing lives in pipeline/probes/).
 */
struct DetectionResult {
    int class_id{-1};
    std::string label;
    float confidence{0.0f};
    float left{0.0f};
    float top{0.0f};
    float width{0.0f};
    float height{0.0f};
    int tracker_id{-1};
    std::string object_id;  ///< UUID assigned to tracked object
    std::any extra;         ///< Backend-specific extra data
};

/**
 * @brief A processed frame with all its detections.
 */
struct FrameEvent {
    int source_id{0};
    std::string source_uri;
    uint64_t frame_number{0};
    double timestamp{0.0};
    std::vector<DetectionResult> detections;
    std::string pipeline_id;
};

/**
 * @brief Processes raw frame data into domain events.
 *
 * The concrete DeepStream implementation lives in the pipeline layer
 * where NvDs SDK headers are available.
 */
class IEventProcessor {
   public:
    virtual ~IEventProcessor() = default;

    /**
     * @brief Process a batch of raw metadata into domain FrameEvents.
     * @param raw_batch_meta Backend-specific batch metadata (e.g. NvDsBatchMeta*).
     * @return One FrameEvent per source in the batch.
     */
    virtual std::vector<FrameEvent> process_batch(const std::any& raw_batch_meta) = 0;

    /**
     * @brief Filter detections by class IDs and confidence threshold.
     * @param detections Input detections to filter.
     * @param class_ids  Allowlist of class IDs (empty = allow all).
     * @param min_confidence Minimum confidence threshold.
     * @return Filtered detections.
     */
    virtual std::vector<DetectionResult> filter_detections(
        const std::vector<DetectionResult>& detections, const std::vector<int>& class_ids,
        float min_confidence = 0.0f) const = 0;
};

}  // namespace engine::domain
