#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace engine::domain {

/** @brief Allowed runtime parameter value types. */
using ParamValue = std::variant<int, float, double, bool, std::string>;

/**
 * @brief Validation constraint for a single runtime parameter.
 */
struct ParamRule {
    std::string name;
    std::string description;
    ParamValue default_value;
    ParamValue min_value;  ///< For numeric types
    ParamValue max_value;  ///< For numeric types
    bool requires_restart{false};
};

/**
 * @brief Registry of all runtime-changeable parameters and their rules.
 *
 * Pure domain logic — no GStreamer or infrastructure dependencies.
 */
class RuntimeParamRules {
   public:
    /** @brief Register a new parameter rule. */
    void register_rule(const std::string& param_name, ParamRule rule);

    /** @brief Check if a parameter exists and can be changed at runtime. */
    bool is_modifiable(const std::string& param_name) const;

    /** @brief Validate a parameter value against its rules. */
    bool validate(const std::string& param_name, const ParamValue& value) const;

    /** @brief Get the default value for a parameter. */
    ParamValue get_default(const std::string& param_name) const;

    /** @brief Get all registered parameter names. */
    std::unordered_set<std::string> get_all_param_names() const;

    /** @brief Check if parameter change requires pipeline restart. */
    bool requires_restart(const std::string& param_name) const;

    /** @brief Create rules with built-in defaults for common parameters. */
    static RuntimeParamRules create_default();

   private:
    std::unordered_map<std::string, ParamRule> rules_;
};

}  // namespace engine::domain
