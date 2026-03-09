#pragma once

#include "engine/core/config/config_types.hpp"

#include <gst/gst.h>
#include <gstnvdsmeta.h>

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::core::messaging {
class IMessageProducer;
}

namespace engine::pipeline::evidence {
class FrameEvidenceCache;
struct FrameObjectSnapshot;
struct FrameCaptureMetadata;
}  // namespace engine::pipeline::evidence

namespace engine::pipeline::extproc {
class FrameEventsExtProcService;
}

namespace engine::pipeline::probes {

/**
 * @brief Semantic object snapshot carried inside one `frame_events` payload.
 *
 * `object_key` stays stable for the same track across frames, while
 * `instance_key` is unique for one emitted frame and is the preferred key for
 * evidence correlation.
 */
struct FrameEventObject {
    std::string object_key;
    std::string instance_key;
    std::string crop_ref;
    uint64_t object_id = 0;
    uint64_t tracker_id = 0;
    int class_id = 0;
    ///< Primary detector label from `NvDsObjectMeta::obj_label`.
    std::string object_type;
    double confidence = 0.0;
    ///< First observed SGIE labels before commit, then stable voted SGIE labels after commit.
    std::vector<std::string> labels;
    ///< Internal stable signature used only for label_change decisions.
    std::string label_signature;
    float left = 0.0F;
    float top = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    std::string parent_object_key;
    std::string parent_instance_key;
    int64_t parent_object_id = -1;
};

/** @brief Last emitted view of one tracked object, used for change detection. */
struct LastEmittedObjectState {
    std::string object_type;
    std::string label_signature;
    int class_id = 0;
    int64_t parent_object_id = -1;
    float left = 0.0F;
    float top = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

struct LabelVoteSample {
    std::string signature;
    std::vector<std::string> labels;
};

struct LabelVoteState {
    std::deque<LabelVoteSample> samples;
    bool provisional_initialized = false;
    std::vector<std::string> provisional_labels;
    std::string committed_signature;
    std::vector<std::string> committed_labels;
};

/** @brief Per-source semantic state used for heartbeat and emit suppression. */
struct PerSourceEmitState {
    int64_t last_emit_ms = 0;
    bool had_detection = false;
    std::unordered_map<uint64_t, LastEmittedObjectState> objects;
    std::unordered_map<uint64_t, LabelVoteState> label_votes;
    std::string last_object_signature;
};

class FrameEventsProbeHandler {
   public:
    FrameEventsProbeHandler() = default;
    ~FrameEventsProbeHandler() = default;

    void configure(const engine::core::config::PipelineConfig& config,
                   const engine::core::config::EventHandlerConfig& handler,
                   engine::core::messaging::IMessageProducer* eproducer,
                   engine::pipeline::evidence::FrameEvidenceCache* cache,
                   engine::pipeline::extproc::FrameEventsExtProcService* ext_proc_service);

    static GstPadProbeReturn on_buffer(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);

   private:
    // Collect all tracked objects visible on this source-frame into one semantic snapshot.
    void collect_frame_objects(NvDsFrameMeta* frame_meta, const std::string& source_name,
                               const std::string& frame_key,
                               std::vector<FrameEventObject>& out_objects) const;
    // Smooth SGIE label jitter with sliding-window majority vote per object.
    void apply_label_majority_vote(PerSourceEmitState& state,
                                   std::vector<FrameEventObject>& objects) const;
    // Apply the emit policy and explain why this frame should be published.
    bool should_emit_message(int source_id, std::vector<FrameEventObject>& objects,
                             int64_t emitted_at_ms, std::vector<std::string>& out_reasons);
    // Publish the canonical one-frame payload consumed by downstream rule engines.
    void publish_frame_message(const engine::pipeline::evidence::FrameCaptureMetadata& meta,
                               const std::vector<std::string>& emit_reason,
                               const std::vector<FrameEventObject>& objects) const;
    void reset_source_state(int source_id);
    double compute_iou(const LastEmittedObjectState& previous,
                       const FrameEventObject& current) const;

    std::string pipeline_id_;
    std::string broker_channel_;
    engine::core::config::FrameEventsConfig config_;
    std::vector<std::string> label_filter_;
    std::unordered_map<int, std::string> source_id_to_name_;
    std::unordered_map<int, PerSourceEmitState> emit_state_;
    int source_width_ = 0;
    int source_height_ = 0;
    std::string handler_id_;
    engine::core::messaging::IMessageProducer* producer_ = nullptr;
    engine::pipeline::evidence::FrameEvidenceCache* cache_ = nullptr;
    engine::pipeline::extproc::FrameEventsExtProcService* ext_proc_service_ = nullptr;
};

}  // namespace engine::pipeline::probes