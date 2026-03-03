#include "engine/pipeline/builder_factory.hpp"
#include "engine/pipeline/builders/source_builder.hpp"
#include "engine/pipeline/builders/muxer_builder.hpp"
#include "engine/pipeline/builders/infer_builder.hpp"
#include "engine/pipeline/builders/tracker_builder.hpp"
#include "engine/pipeline/builders/analytics_builder.hpp"
#include "engine/pipeline/builders/tiler_builder.hpp"
#include "engine/pipeline/builders/osd_builder.hpp"
#include "engine/pipeline/builders/encoder_builder.hpp"
#include "engine/pipeline/builders/sink_builder.hpp"
#include "engine/pipeline/builders/msgconv_builder.hpp"
#include "engine/pipeline/builders/msgbroker_builder.hpp"
#include "engine/pipeline/builders/demuxer_builder.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::pipeline {

BuilderFactory::BuilderFactory(GstElement* bin) : bin_(bin) {
    register_defaults();
}

std::unique_ptr<engine::core::builders::IElementBuilder> BuilderFactory::create(
    const std::string& type) {
    auto it = creators_.find(type);
    if (it == creators_.end()) {
        LOG_E("Unknown element type: '{}'", type);
        return nullptr;
    }
    return it->second(bin_);
}

void BuilderFactory::register_defaults() {
    creators_["nvmultiurisrcbin"] = [](GstElement* b) {
        return std::make_unique<builders::SourceBuilder>(b);
    };
    creators_["nvstreammux"] = [](GstElement* b) {
        return std::make_unique<builders::MuxerBuilder>(b);
    };
    creators_["nvinfer"] = [](GstElement* b) {
        return std::make_unique<builders::InferBuilder>(b);
    };
    creators_["nvtracker"] = [](GstElement* b) {
        return std::make_unique<builders::TrackerBuilder>(b);
    };
    creators_["nvdsanalytics"] = [](GstElement* b) {
        return std::make_unique<builders::AnalyticsBuilder>(b);
    };
    creators_["nvmultistreamtiler"] = [](GstElement* b) {
        return std::make_unique<builders::TilerBuilder>(b);
    };
    creators_["nvdsosd"] = [](GstElement* b) { return std::make_unique<builders::OsdBuilder>(b); };
    creators_["nvv4l2h264enc"] = [](GstElement* b) {
        return std::make_unique<builders::EncoderBuilder>(b);
    };
    creators_["nvv4l2h265enc"] = [](GstElement* b) {
        return std::make_unique<builders::EncoderBuilder>(b);
    };
    creators_["rtspclientsink"] = [](GstElement* b) {
        return std::make_unique<builders::SinkBuilder>(b);
    };
    creators_["fakesink"] = [](GstElement* b) {
        return std::make_unique<builders::SinkBuilder>(b);
    };
    creators_["filesink"] = [](GstElement* b) {
        return std::make_unique<builders::SinkBuilder>(b);
    };
    creators_["nvmsgconv"] = [](GstElement* b) {
        return std::make_unique<builders::MsgconvBuilder>(b);
    };
    creators_["nvmsgbroker"] = [](GstElement* b) {
        return std::make_unique<builders::MsgbrokerBuilder>(b);
    };
    creators_["nvstreamdemux"] = [](GstElement* b) {
        return std::make_unique<builders::DemuxerBuilder>(b);
    };
}

}  // namespace engine::pipeline
