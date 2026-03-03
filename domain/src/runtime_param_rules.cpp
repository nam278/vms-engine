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

    rules.register_rule("confidence_threshold",
                        {"confidence_threshold", "Minimum detection confidence (0.0 – 1.0)", 0.5f,
                         0.0f, 1.0f, false});

    rules.register_rule("tracker_enabled", {"tracker_enabled", "Enable/disable object tracker",
                                            true, false, true, true});  // requires restart

    rules.register_rule("inference_interval",
                        {"inference_interval",
                         "Skip N batches between inferences (0 = every frame)", 0, 0, 30, false});

    rules.register_rule("bitrate",
                        {"bitrate", "Encoder bitrate in bps", 4000000, 500000, 20000000, false});

    return rules;
}

}  // namespace engine::domain
