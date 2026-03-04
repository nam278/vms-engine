/**
 * @file yaml_parser_handlers.cpp
 * @brief Parses the `event_handlers:` section into vector<EventHandlerConfig>.
 */
#include "engine/infrastructure/config_parser/yaml_config_parser.hpp"
#include "yaml_parser_helpers.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::infrastructure::config_parser {

void YamlConfigParser::parse_handlers(const void* node_ptr,
                                      std::vector<engine::core::config::EventHandlerConfig>& out) {
    const auto& node = *static_cast<const YAML::Node*>(node_ptr);
    if (!node || !node.IsSequence())
        return;

    using helpers::yaml_bool;
    using helpers::yaml_int;
    using helpers::yaml_str;
    using helpers::yaml_string_list;

    out.clear();
    for (const auto& h : node) {
        engine::core::config::EventHandlerConfig handler;

        handler.id = yaml_str(h, "id");
        handler.enable = yaml_bool(h, "enable", true);
        handler.type = yaml_str(h, "type");
        handler.probe_element = yaml_str(h, "probe_element");
        handler.pad_name = yaml_str(h, "pad_name");
        if (handler.pad_name.empty())
            handler.pad_name = "src";  // default
        handler.source_element = yaml_str(h, "source_element");
        handler.trigger = yaml_str(h, "trigger");
        handler.channel = yaml_str(h, "channel");
        handler.label_filter = yaml_string_list(h, "label_filter");

        // Smart record specific
        handler.pre_event_sec = yaml_int(h, "pre_event_sec", 2);
        handler.post_event_sec = yaml_int(h, "post_event_sec", 20);
        handler.min_interval_sec = yaml_int(h, "min_interval_sec", 2);
        handler.max_concurrent_recordings = yaml_int(h, "max_concurrent_recordings", 0);

        // Crop objects specific
        handler.save_dir = yaml_str(h, "save_dir");
        handler.capture_interval_sec = yaml_int(h, "capture_interval_sec", 5);
        handler.image_quality = yaml_int(h, "image_quality", 85);
        handler.save_full_frame = yaml_bool(h, "save_full_frame", true);

        // Cleanup sub-section
        if (h["cleanup"] && h["cleanup"].IsMap()) {
            engine::core::config::CleanupConfig cleanup;
            const auto& c = h["cleanup"];
            cleanup.stale_object_timeout_min = yaml_int(c, "stale_object_timeout_min", 5);
            cleanup.check_interval_batches = yaml_int(c, "check_interval_batches", 30);
            cleanup.old_dirs_max_days = yaml_int(c, "old_dirs_max_days", 7);
            handler.cleanup = cleanup;
        }

        // External processor sub-section
        if (h["ext_processor"] && h["ext_processor"].IsMap()) {
            engine::core::config::ExtProcessorConfig ext;
            const auto& ep = h["ext_processor"];
            ext.enable = yaml_bool(ep, "enable", false);
            ext.min_interval_sec = yaml_int(ep, "min_interval_sec", 1);

            if (ep["rules"] && ep["rules"].IsSequence()) {
                for (const auto& r : ep["rules"]) {
                    engine::core::config::ExtProcessorRule rule;
                    rule.label = yaml_str(r, "label");
                    rule.endpoint = yaml_str(r, "endpoint");
                    rule.result_path = yaml_str(r, "result_path");
                    rule.display_path = yaml_str(r, "display_path");

                    if (r["params"] && r["params"].IsMap()) {
                        for (const auto& kv : r["params"]) {
                            rule.params[kv.first.as<std::string>()] = kv.second.as<std::string>("");
                        }
                    }

                    ext.rules.push_back(std::move(rule));
                }
            }

            handler.ext_processor = ext;
        }

        out.push_back(std::move(handler));
    }

    LOG_D("Parsed {} event handlers", out.size());
}

}  // namespace engine::infrastructure::config_parser
