/**
 * @file yaml_parser_processing.cpp
 * @brief Parses the `processing:` section into ProcessingConfig.
 */
#include "engine/infrastructure/config_parser/yaml_config_parser.hpp"
#include "yaml_parser_helpers.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::infrastructure::config_parser {

void YamlConfigParser::parse_processing(const void* node_ptr,
                                        engine::core::config::ProcessingConfig& out,
                                        const engine::core::config::QueueConfig& defaults) {
    const auto& node = *static_cast<const YAML::Node*>(node_ptr);
    if (!node || !node.IsMap())
        return;

    using helpers::yaml_bool;
    using helpers::yaml_int;
    using helpers::yaml_str;

    // ── elements[] ──
    out.elements.clear();
    if (node["elements"] && node["elements"].IsSequence()) {
        for (const auto& elem_node : node["elements"]) {
            engine::core::config::ProcessingElementConfig elem;

            elem.id = yaml_str(elem_node, "id");
            elem.type = yaml_str(elem_node, "type");
            elem.role = yaml_str(elem_node, "role");

            // nvinfer properties
            elem.unique_id = yaml_int(elem_node, "unique_id", 0);
            elem.config_file = yaml_str(elem_node, "config_file");
            elem.process_mode = yaml_int(elem_node, "process_mode", 1);
            elem.interval = yaml_int(elem_node, "interval", 0);
            elem.batch_size = yaml_int(elem_node, "batch_size", 4);
            elem.gpu_id = yaml_int(elem_node, "gpu_id", 0);
            elem.operate_on_gie_id = yaml_int(elem_node, "operate_on_gie_id", -1);
            elem.operate_on_class_ids = yaml_str(elem_node, "operate_on_class_ids");

            // nvtracker properties
            elem.ll_lib_file = yaml_str(elem_node, "ll_lib_file");
            elem.ll_config_file = yaml_str(elem_node, "ll_config_file");
            elem.tracker_width = yaml_int(elem_node, "tracker_width", 640);
            elem.tracker_height = yaml_int(elem_node, "tracker_height", 640);
            elem.compute_hw = yaml_int(elem_node, "compute_hw", 0);
            elem.display_tracking_id = yaml_bool(elem_node, "display_tracking_id", true);
            elem.user_meta_pool_size = yaml_int(elem_node, "user_meta_pool_size", 512);

            // Inline queue
            if (elem_node["queue"]) {
                elem.has_queue = true;
                YAML::Node q_node = elem_node["queue"];
                elem.queue = resolve_queue(static_cast<const void*>(&q_node), defaults);
            }

            out.elements.push_back(std::move(elem));
        }
    }

    LOG_D("Parsed processing: {} elements", out.elements.size());
}

}  // namespace engine::infrastructure::config_parser
