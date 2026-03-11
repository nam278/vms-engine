#include "engine/domain/runtime_param_rules.hpp"

namespace engine::domain {

void RuntimeParamRules::register_rule(const std::string& param_name, ParamRule rule) {
    rules_[param_name] = std::move(rule);
}

bool RuntimeParamRules::is_modifiable(const std::string& param_name) const {
    return rules_.find(param_name) != rules_.end();
}

bool RuntimeParamRules::validate(const std::string& param_name, const ParamValue& value) const {
    auto it = rules_.find(param_name);
    if (it == rules_.end())
        return false;
    // Type must match default_value variant index
    return value.index() == it->second.default_value.index();
}

ParamValue RuntimeParamRules::get_default(const std::string& param_name) const {
    auto it = rules_.find(param_name);
    if (it == rules_.end())
        return {};
    return it->second.default_value;
}

std::unordered_set<std::string> RuntimeParamRules::get_all_param_names() const {
    std::unordered_set<std::string> names;
    for (const auto& [key, _] : rules_)
        names.insert(key);
    return names;
}

bool RuntimeParamRules::requires_restart(const std::string& param_name) const {
    auto it = rules_.find(param_name);
    if (it == rules_.end())
        return true;  // Unknown params require restart
    return it->second.requires_restart;
}

RuntimeParamRules RuntimeParamRules::create_default() {
    RuntimeParamRules rules;

    rules.register_rule(
        "osd.display_bbox",
        {"osd.display_bbox", "Enable/disable OSD bounding boxes", true, false, true, false});

    rules.register_rule("osd.display_text", {"osd.display_text", "Enable/disable OSD text labels",
                                             true, false, true, false});

    return rules;
}

}  // namespace engine::domain
