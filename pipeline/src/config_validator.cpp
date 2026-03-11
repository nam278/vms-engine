#include "engine/pipeline/config_validator.hpp"
#include "engine/core/utils/logger.hpp"
#include <unordered_set>

namespace engine::pipeline {

bool ConfigValidator::validate(const engine::core::config::PipelineConfig& config) const {
    errors_.clear();
    bool ok = true;

    ok &= validate_pipeline_meta(config.pipeline);
    ok &= validate_sources(config.sources);
    ok &= validate_processing(config.processing);
    ok &= validate_visuals(config.visuals);
    ok &= validate_outputs(config.outputs);
    ok &= validate_handlers(config.event_handlers);

    if (!ok) {
        for (const auto& e : errors_) {
            LOG_E("Config validation: {}", e);
        }
    }
    return ok;
}

bool ConfigValidator::validate_pipeline_meta(
    const engine::core::config::PipelineMetaConfig& meta) const {
    bool ok = true;
    if (meta.id.empty()) {
        errors_.push_back("pipeline.id is required");
        ok = false;
    }
    return ok;
}

bool ConfigValidator::validate_sources(const engine::core::config::SourcesConfig& sources) const {
    bool ok = true;
    if (sources.type != "nvmultiurisrcbin" && sources.type != "nvurisrcbin") {
        errors_.push_back("sources.type must be 'nvmultiurisrcbin' or 'nvurisrcbin'");
        ok = false;
    }
    const int batch_size =
        sources.mux.batch_size > 0 ? sources.mux.batch_size : sources.max_batch_size;
    const int max_sources = sources.mux.max_sources > 0 ? sources.mux.max_sources : batch_size;
    if (batch_size < 1) {
        errors_.push_back("sources.mux.batch_size (or sources.max_batch_size) must be >= 1");
        ok = false;
    }
    if (sources.width <= 0 || sources.height <= 0) {
        errors_.push_back("sources.width and sources.height must be > 0");
        ok = false;
    }
    if (sources.cameras.size() > static_cast<std::size_t>(max_sources)) {
        errors_.push_back("sources.cameras cannot exceed sources.mux.max_sources");
        ok = false;
    }
    if (sources.type == "nvurisrcbin") {
        if (sources.mux.implementation != "new" && sources.mux.implementation != "legacy") {
            errors_.push_back("sources.mux.implementation must be 'new' or 'legacy'");
            ok = false;
        }
        for (const auto& elem : sources.branch.elements) {
            if (elem.type != "nvvideoconvert" && elem.type != "capsfilter" &&
                elem.type != "queue") {
                errors_.push_back(
                    "sources.branch.elements only support 'nvvideoconvert', 'capsfilter', or "
                    "'queue'");
                ok = false;
            }
            if (elem.type == "capsfilter" && elem.enabled && elem.caps.empty()) {
                errors_.push_back("enabled sources.branch capsfilter requires a caps string");
                ok = false;
            }
        }
    }
    if (sources.mux.max_latency_ns == 0 && sources.type == "nvurisrcbin") {
        errors_.push_back("sources.mux.max_latency_ns should be > 0 for standalone nvstreammux");
        ok = false;
    }
    return ok;
}

bool ConfigValidator::validate_processing(
    const engine::core::config::ProcessingConfig& processing) const {
    bool ok = true;
    std::unordered_set<std::string> ids;

    for (const auto& elem : processing.elements) {
        if (elem.id.empty()) {
            errors_.push_back("processing element has empty id");
            ok = false;
        }
        if (!ids.insert(elem.id).second) {
            errors_.push_back("duplicate processing element id: " + elem.id);
            ok = false;
        }
        if (elem.type.empty()) {
            errors_.push_back("processing element '" + elem.id + "' has empty type");
            ok = false;
        }
    }
    return ok;
}

bool ConfigValidator::validate_visuals(const engine::core::config::VisualsConfig& visuals) const {
    bool ok = true;
    if (!visuals.enable)
        return true;

    for (const auto& elem : visuals.elements) {
        if (elem.id.empty()) {
            errors_.push_back("visuals element has empty id");
            ok = false;
        }
    }
    return ok;
}

bool ConfigValidator::validate_outputs(
    const std::vector<engine::core::config::OutputConfig>& outputs) const {
    bool ok = true;
    std::unordered_set<std::string> ids;

    for (const auto& output : outputs) {
        if (output.id.empty()) {
            errors_.push_back("output has empty id");
            ok = false;
        }
        if (!ids.insert(output.id).second) {
            errors_.push_back("duplicate output id: " + output.id);
            ok = false;
        }
        if (output.elements.empty()) {
            errors_.push_back("output '" + output.id + "' has no elements");
            ok = false;
        }
    }
    return ok;
}

bool ConfigValidator::validate_handlers(
    const std::vector<engine::core::config::EventHandlerConfig>& handlers) const {
    bool ok = true;
    for (const auto& h : handlers) {
        if (!h.enable)
            continue;
        if (h.id.empty()) {
            errors_.push_back("event handler has empty id");
            ok = false;
        }
        if (h.type.empty()) {
            errors_.push_back("event handler '" + h.id + "' has empty type");
            ok = false;
        }
        if (h.probe_element.empty()) {
            errors_.push_back("event handler '" + h.id + "' has empty probe_element");
            ok = false;
        }
    }
    return ok;
}

}  // namespace engine::pipeline
