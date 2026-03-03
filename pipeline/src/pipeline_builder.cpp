#include "engine/pipeline/pipeline_builder.hpp"
#include "engine/pipeline/block_builders/source_block_builder.hpp"
#include "engine/pipeline/block_builders/processing_block_builder.hpp"
#include "engine/pipeline/block_builders/visuals_block_builder.hpp"
#include "engine/pipeline/block_builders/outputs_block_builder.hpp"
#include "engine/core/utils/logger.hpp"

#include <gst/gst.h>
#include <stdexcept>

namespace engine::pipeline {

PipelineBuilder::~PipelineBuilder() {
    cleanup_pipeline();
}

void PipelineBuilder::cleanup_pipeline() {
    if (pipeline_) {
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }
    tails_.clear();
}

bool PipelineBuilder::build(const engine::core::config::PipelineConfig& config,
                            GMainLoop* main_loop) {
    loop_ = main_loop;
    tails_.clear();

    // ── Create top-level GstPipeline ────────────────────────────────
    const std::string pipeline_id =
        config.pipeline.id.empty() ? "vms_engine_pipeline" : config.pipeline.id;

    cleanup_pipeline();  // in case build() is called more than once

    pipeline_ = gst_pipeline_new(pipeline_id.c_str());
    if (!pipeline_) {
        LOG_C("Build failed: gst_pipeline_new('{}') returned null", pipeline_id);
        return false;
    }
    LOG_D("Created GstPipeline '{}'", pipeline_id);

    try {
        // ── Phase 1: Source ─────────────────────────────────────────
        {
            block_builders::SourceBlockBuilder blk(pipeline_, tails_);
            if (!blk.build(config)) {
                LOG_E("Phase 1 (source) failed");
                cleanup_pipeline();
                return false;
            }
            LOG_D("Phase 1 (source) OK");
        }

        // ── Phase 2: Processing ─────────────────────────────────────
        {
            block_builders::ProcessingBlockBuilder blk(pipeline_, tails_);
            if (!blk.build(config)) {
                LOG_E("Phase 2 (processing) failed");
                cleanup_pipeline();
                return false;
            }
            LOG_D("Phase 2 (processing) OK");
        }

        // ── Phase 3: Visuals ────────────────────────────────────────
        {
            block_builders::VisualsBlockBuilder blk(pipeline_, tails_);
            if (!blk.build(config)) {
                LOG_E("Phase 3 (visuals) failed");
                cleanup_pipeline();
                return false;
            }
            LOG_D("Phase 3 (visuals) OK");
        }

        // ── Phase 4: Outputs ────────────────────────────────────────
        {
            block_builders::OutputsBlockBuilder blk(pipeline_, tails_);
            if (!blk.build(config)) {
                LOG_E("Phase 4 (outputs) failed");
                cleanup_pipeline();
                return false;
            }
            LOG_D("Phase 4 (outputs) OK");
        }

        // ── DOT graph dump (if enabled) ─────────────────────────────
        if (!config.pipeline.dot_file_dir.empty()) {
            gst_debug_bin_to_dot_file(GST_BIN(pipeline_), GST_DEBUG_GRAPH_SHOW_ALL,
                                      (pipeline_id + "_build_graph").c_str());
            LOG_I("Pipeline DOT graph written to '{}_build_graph.dot'", pipeline_id);
        }

        LOG_I("Pipeline '{}' build complete", pipeline_id);
        return true;

    } catch (const std::exception& e) {
        LOG_C("Build failed (exception): {}", e.what());
        cleanup_pipeline();
        return false;
    } catch (...) {
        LOG_C("Build failed (unknown exception)");
        cleanup_pipeline();
        return false;
    }
}

GstElement* PipelineBuilder::get_pipeline() const {
    return pipeline_;
}

}  // namespace engine::pipeline
