/**
 * @file yaml_parser_visuals.cpp
 * @brief Parses the `visuals:` section into VisualsConfig.
 */
#include "engine/infrastructure/config_parser/yaml_config_parser.hpp"
#include "yaml_parser_helpers.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::infrastructure::config_parser {

void YamlConfigParser::parse_visuals(const void* node_ptr, engine::core::config::VisualsConfig& out,
                                     const engine::core::config::QueueConfig& defaults) {
    const auto& node = *static_cast<const YAML::Node*>(node_ptr);
    if (!node || !node.IsMap())
        return;

    using helpers::yaml_bool;
    using helpers::yaml_int;
    using helpers::yaml_str;

    out.enable = yaml_bool(node, "enable", true);

    // ── elements[] ──
    out.elements.clear();
    if (node["elements"] && node["elements"].IsSequence()) {
        for (const auto& elem_node : node["elements"]) {
            engine::core::config::VisualsElementConfig elem;

            elem.id = yaml_str(elem_node, "id");
            elem.type = yaml_str(elem_node, "type");
            elem.gpu_id = yaml_int(elem_node, "gpu_id", 0);

            // nvmultistreamtiler
            elem.rows = yaml_int(elem_node, "rows", 1);
            elem.columns = yaml_int(elem_node, "columns", 1);
            elem.width = yaml_int(elem_node, "width", 1920);
            elem.height = yaml_int(elem_node, "height", 1080);

            // nvdsosd
            elem.process_mode = yaml_int(elem_node, "process_mode", 1);
            elem.display_bbox = yaml_bool(elem_node, "display_bbox", true);
            elem.display_text = yaml_bool(elem_node, "display_text", false);
            elem.display_mask = yaml_bool(elem_node, "display_mask", false);
            elem.border_width = yaml_int(elem_node, "border_width", 2);

            // Inline queue
            if (elem_node["queue"]) {
                elem.has_queue = true;
                YAML::Node q_node = elem_node["queue"];
                elem.queue = resolve_queue(static_cast<const void*>(&q_node), defaults);
            }

            out.elements.push_back(std::move(elem));
        }
    }

    // ── output_queue ──
    if (node["output_queue"]) {
        YAML::Node oq_node = node["output_queue"];
        out.output_queue = resolve_queue(static_cast<const void*>(&oq_node), defaults);
    }

    LOG_D("Parsed visuals: enable={} {} elements", out.enable, out.elements.size());
}

}  // namespace engine::infrastructure::config_parser
