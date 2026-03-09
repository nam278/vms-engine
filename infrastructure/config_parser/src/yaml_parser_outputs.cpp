/**
 * @file yaml_parser_outputs.cpp
 * @brief Parses the `outputs:` section into vector<OutputConfig>.
 */
#include "engine/infrastructure/config_parser/yaml_config_parser.hpp"
#include "yaml_parser_helpers.hpp"
#include "engine/core/utils/logger.hpp"

namespace engine::infrastructure::config_parser {

void YamlConfigParser::parse_outputs(const void* node_ptr,
                                     std::vector<engine::core::config::OutputConfig>& out,
                                     const engine::core::config::QueueConfig& defaults) {
    const auto& node = *static_cast<const YAML::Node*>(node_ptr);
    if (!node || !node.IsSequence())
        return;

    using helpers::yaml_bool;
    using helpers::yaml_int;
    using helpers::yaml_str;

    out.clear();
    for (const auto& out_node : node) {
        engine::core::config::OutputConfig output;
        output.id = yaml_str(out_node, "id");
        output.type = yaml_str(out_node, "type");

        // ── elements[] ──
        if (out_node["elements"] && out_node["elements"].IsSequence()) {
            for (const auto& elem_node : out_node["elements"]) {
                engine::core::config::OutputElementConfig elem;

                elem.id = yaml_str(elem_node, "id");
                elem.type = yaml_str(elem_node, "type");
                elem.gpu_id = yaml_int(elem_node, "gpu_id", 0);

                // Flat properties — each element type uses a different subset
                elem.caps = yaml_str(elem_node, "caps");
                elem.nvbuf_memory_type = yaml_str(elem_node, "nvbuf_memory_type");
                elem.src_crop = yaml_str(elem_node, "src_crop");
                elem.dest_crop = yaml_str(elem_node, "dest_crop");
                elem.bitrate = yaml_int(elem_node, "bitrate", 0);
                elem.control_rate = yaml_str(elem_node, "control_rate");
                elem.profile = yaml_str(elem_node, "profile");
                elem.iframeinterval = yaml_int(elem_node, "iframeinterval", 0);
                elem.location = yaml_str(elem_node, "location");
                elem.protocols = yaml_str(elem_node, "protocols");

                // Inline queue
                if (elem_node["queue"]) {
                    elem.has_queue = true;
                    YAML::Node q_node = elem_node["queue"];
                    elem.queue = resolve_queue(static_cast<const void*>(&q_node), defaults);
                }

                output.elements.push_back(std::move(elem));
            }
        }

        out.push_back(std::move(output));
    }

    LOG_D("Parsed {} output blocks", out.size());
}

}  // namespace engine::infrastructure::config_parser
