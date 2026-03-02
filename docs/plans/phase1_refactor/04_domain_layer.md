# Plan 04 — Domain Layer (Business Logic)

> Migrate `lantanav2/domain/` → `vms-engine/domain/`.
> **All 5 source files are empty (0 lines)** in lantanav2. This plan defines the interface contracts and basic implementations from scratch.

---

## Prerequisites

- Plan 02 completed (core interfaces compile)

## Context

The domain layer in lantanav2 was scaffolded but never filled in:

| File | Lines |
| --- | --- |
| `event_processor.hpp` | 0 |
| `metadata_parser.hpp` | 0 |
| `runtime_param_rules.hpp` | 0 |
| `event_processor.cpp` | 0 |
| `metadata_parser.cpp` | 1 (empty) |

In the refactored vms-engine, these become real domain services that encapsulate business rules decoupled from any framework (GStreamer, DeepStream SDK, infrastructure).

---

## Deliverables

- [ ] `domain/include/engine/domain/event_processor.hpp` — Interface for processing detection events
- [ ] `domain/include/engine/domain/metadata_parser.hpp` — Interface for parsing frame metadata
- [ ] `domain/include/engine/domain/runtime_param_rules.hpp` — Runtime parameter validation rules
- [ ] `domain/src/event_processor.cpp` — Stub or default implementation
- [ ] `domain/src/metadata_parser.cpp` — Stub or default implementation
- [ ] `domain/CMakeLists.txt` updated
- [ ] Domain library compiles

---

## Interface Definitions

### 4.1 — IEventProcessor

Processes raw detection events from probes and converts them to domain events for publishing.

```cpp
// domain/include/engine/domain/event_processor.hpp
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <any>

namespace engine::domain {

/// Represents a single detection from a frame
struct DetectionResult {
    int         class_id{-1};
    std::string label;
    float       confidence{0.0f};
    float       left{0.0f};
    float       top{0.0f};
    float       width{0.0f};
    float       height{0.0f};
    int         tracker_id{-1};
    std::string object_id;             // UUID assigned to tracked object
    std::any    extra;                  // Backend-specific extra data
};

/// Represents a processed frame with all its detections
struct FrameEvent {
    int                          source_id{0};
    std::string                  source_uri;
    uint64_t                     frame_number{0};
    double                       timestamp{0.0};
    std::vector<DetectionResult> detections;
    std::string                  pipeline_id;
};

/// Interface — processes raw frame data into domain events
class IEventProcessor {
public:
    virtual ~IEventProcessor() = default;

    /// Process a batch of frame events (one per source in muxed pipeline)
    virtual std::vector<FrameEvent> process_batch(
        const std::any& raw_batch_meta) = 0;

    /// Filter detections by class IDs, confidence threshold, etc.
    virtual std::vector<DetectionResult> filter_detections(
        const std::vector<DetectionResult>& detections,
        const std::vector<int>& class_ids,
        float min_confidence = 0.0f) const = 0;
};

} // namespace engine::domain
```

### 4.2 — IMetadataParser

Extracts structured metadata from backend-specific batch metadata pointers.

```cpp
// domain/include/engine/domain/metadata_parser.hpp
#pragma once

#include "engine/domain/event_processor.hpp"
#include <any>
#include <string>
#include <vector>

namespace engine::domain {

/// Interface — parses backend-specific metadata into domain types
class IMetadataParser {
public:
    virtual ~IMetadataParser() = default;

    /// Parse batch metadata into a list of FrameEvents
    virtual std::vector<FrameEvent> parse_batch(
        const std::any& batch_meta) = 0;

    /// Parse a single frame's object metadata into DetectionResults
    virtual std::vector<DetectionResult> parse_frame_objects(
        const std::any& frame_meta) = 0;

    /// Extract source URI from frame metadata
    virtual std::string get_source_uri(
        const std::any& frame_meta, int source_id) const = 0;
};

} // namespace engine::domain
```

### 4.3 — RuntimeParamRules

Defines which parameters can be changed at runtime and their validation rules.

```cpp
// domain/include/engine/domain/runtime_param_rules.hpp
#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace engine::domain {

/// Allowed runtime parameter value types
using ParamValue = std::variant<int, float, double, bool, std::string>;

/// Validation constraint for a single runtime parameter
struct ParamRule {
    std::string  name;
    std::string  description;
    ParamValue   default_value;
    ParamValue   min_value;      // For numeric types
    ParamValue   max_value;      // For numeric types
    bool         requires_restart{false};
};

/// Registry of all runtime-changeable parameters
class RuntimeParamRules {
public:
    /// Register a new parameter rule
    void register_rule(const std::string& param_name, ParamRule rule);

    /// Check if a parameter exists and can be changed at runtime
    bool is_modifiable(const std::string& param_name) const;

    /// Validate a parameter value against its rules
    bool validate(const std::string& param_name, const ParamValue& value) const;

    /// Get the default value for a parameter
    ParamValue get_default(const std::string& param_name) const;

    /// Get all registered parameter names
    std::unordered_set<std::string> get_all_param_names() const;

    /// Check if parameter change requires pipeline restart
    bool requires_restart(const std::string& param_name) const;

    /// Built-in rules for common parameters
    static RuntimeParamRules create_default();

private:
    std::unordered_map<std::string, ParamRule> rules_;
};

} // namespace engine::domain
```

---

## Source Implementations

### 4.4 — Stub EventProcessor

```cpp
// domain/src/event_processor.cpp
// Default stub — actual implementation in pipeline layer
// (DeepStream-specific parsing lives in pipeline/probes/ and pipeline/event_handlers/)
```

This file will remain minimal. The actual `IEventProcessor` implementation will live in the pipeline layer (where it has access to DeepStream SDK types). The domain layer only defines the interface and domain types.

### 4.5 — RuntimeParamRules Implementation

```cpp
// domain/src/runtime_param_rules.cpp
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
    if (it == rules_.end()) return false;
    // Type must match default_value variant index
    return value.index() == it->second.default_value.index();
}

ParamValue RuntimeParamRules::get_default(const std::string& param_name) const {
    auto it = rules_.find(param_name);
    if (it == rules_.end()) return {};
    return it->second.default_value;
}

std::unordered_set<std::string> RuntimeParamRules::get_all_param_names() const {
    std::unordered_set<std::string> names;
    for (const auto& [key, _] : rules_) names.insert(key);
    return names;
}

bool RuntimeParamRules::requires_restart(const std::string& param_name) const {
    auto it = rules_.find(param_name);
    if (it == rules_.end()) return true; // Unknown params require restart
    return it->second.requires_restart;
}

RuntimeParamRules RuntimeParamRules::create_default() {
    RuntimeParamRules rules;
    // Common runtime-modifiable parameters
    rules.register_rule("confidence_threshold", {
        "confidence_threshold",
        "Minimum detection confidence (0.0 – 1.0)",
        0.5f, 0.0f, 1.0f, false
    });
    rules.register_rule("tracker_enabled", {
        "tracker_enabled",
        "Enable/disable object tracker",
        true, false, true, true // requires restart
    });
    return rules;
}

} // namespace engine::domain
```

---

## CMakeLists.txt

```cmake
# domain/CMakeLists.txt
add_library(vms_engine_domain STATIC
    src/runtime_param_rules.cpp
)

target_include_directories(vms_engine_domain
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(vms_engine_domain
    PUBLIC vms_engine_core
)
```

---

## Verification

```bash
# 1. Compile domain library
cmake --build build --target vms_engine_domain -- -j$(nproc)

# 2. Check no infrastructure or pipeline dependencies
grep -r "infrastructure\|pipeline\|deepstream\|gst\|nvds" domain/ && echo "FAIL" || echo "PASS"

# 3. Check namespace
grep -rL "engine::domain" domain/include/ | head -10
```

---

## Checklist

- [ ] `event_processor.hpp` — interface + `FrameEvent` / `DetectionResult` types
- [ ] `metadata_parser.hpp` — interface for backend-agnostic metadata parsing
- [ ] `runtime_param_rules.hpp` — parameter validation rules + registry
- [ ] `runtime_param_rules.cpp` — implementation with default rules
- [ ] `domain/CMakeLists.txt` updated
- [ ] `vms_engine_domain` compiles
- [ ] Zero infrastructure/pipeline/backend dependencies
