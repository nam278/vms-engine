---
goal: "Plan 04 — Domain Layer: Business Logic (Pure, Framework-Free)"
version: "1.0"
date_created: "2025-01-15"
last_updated: "2025-07-17"
owner: "VMS Engine Team"
status: "Planned"
tags: [domain, business-logic, event-processor, metadata-parser, runtime-params, c++17]
---

# Plan 04 — Domain Layer (Business Logic)

![Status: Planned](https://img.shields.io/badge/status-Planned-blue)

Create all files in `domain/` from scratch.
**Business rules and interfaces defined cleanly** — no framework (GStreamer, DeepStream SDK) in this layer.
The domain layer defines types and interfaces that pipeline and infrastructure implement.

---

## 1. Requirements & Constraints

- **REQ-001**: Plan 02 completed (core interfaces compile).
- **REQ-002**: Domain layer contains pure business logic — no GStreamer, no DeepStream SDK headers.
- **REQ-003**: Only depends on `engine::core::config::` types and C++ standard library.
- **REQ-004**: Actual DeepStream-specific metadata parsing lives in `pipeline/probes/` — domain defines interfaces only.
- **CON-001**: No infrastructure dependencies (`#include "engine/infrastructure/"` forbidden).
- **CON-002**: No pipeline dependencies (`#include "engine/pipeline/"` forbidden).
- **CON-003**: All files use `engine::domain::` namespace.
- **GUD-001**: Use `std::any` as opaque handle for backend-specific metadata — keeps domain framework-free.
- **GUD-002**: `RuntimeParamRules` uses `std::variant<int, float, double, bool, std::string>` for type-safe values.
- **PAT-001**: Interface-first — domain defines `IEventProcessor` and `IMetadataParser`, pipeline implements them.

---

## 2. Implementation Steps

### Phase 1 — Event Processing Types & Interface

**GOAL-001**: Create `IEventProcessor` with `DetectionResult` and `FrameEvent` domain types.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-001 | Create `DetectionResult` struct (class_id, label, confidence, bbox, tracker_id, object_id, extra as `std::any`) | ☐ | |
| TASK-002 | Create `FrameEvent` struct (source_id, source_uri, frame_number, timestamp, detections, pipeline_id) | ☐ | |
| TASK-003 | Create `IEventProcessor` interface (process_batch, filter_detections) | ☐ | |

```cpp
// domain/include/engine/domain/event_processor.hpp
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <any>

namespace engine::domain {

struct DetectionResult {
    int         class_id{-1};
    std::string label;
    float       confidence{0.0f};
    float       left{0.0f};
    float       top{0.0f};
    float       width{0.0f};
    float       height{0.0f};
    int         tracker_id{-1};
    std::string object_id;
    std::any    extra;
};

struct FrameEvent {
    int                          source_id{0};
    std::string                  source_uri;
    uint64_t                     frame_number{0};
    double                       timestamp{0.0};
    std::vector<DetectionResult> detections;
    std::string                  pipeline_id;
};

class IEventProcessor {
public:
    virtual ~IEventProcessor() = default;
    virtual std::vector<FrameEvent> process_batch(
        const std::any& raw_batch_meta) = 0;
    virtual std::vector<DetectionResult> filter_detections(
        const std::vector<DetectionResult>& detections,
        const std::vector<int>& class_ids,
        float min_confidence = 0.0f) const = 0;
};

} // namespace engine::domain
```

### Phase 2 — Metadata Parser Interface

**GOAL-002**: Create `IMetadataParser` for backend-agnostic metadata extraction.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-004 | Create `IMetadataParser` interface (parse_batch, parse_frame_objects, get_source_uri) | ☐ | |

```cpp
// domain/include/engine/domain/metadata_parser.hpp
#pragma once

#include "engine/domain/event_processor.hpp"
#include <any>
#include <string>
#include <vector>

namespace engine::domain {

class IMetadataParser {
public:
    virtual ~IMetadataParser() = default;
    virtual std::vector<FrameEvent> parse_batch(
        const std::any& batch_meta) = 0;
    virtual std::vector<DetectionResult> parse_frame_objects(
        const std::any& frame_meta) = 0;
    virtual std::string get_source_uri(
        const std::any& frame_meta, int source_id) const = 0;
};

} // namespace engine::domain
```

### Phase 3 — Runtime Parameter Rules

**GOAL-003**: Create `RuntimeParamRules` registry with validation and default rules.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-005 | Define `ParamValue` variant and `ParamRule` struct | ☐ | |
| TASK-006 | Implement `RuntimeParamRules` class (register_rule, is_modifiable, validate, get_default, requires_restart) | ☐ | |
| TASK-007 | Implement `create_default()` with common runtime-modifiable parameters | ☐ | |

```cpp
// domain/include/engine/domain/runtime_param_rules.hpp
#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace engine::domain {

using ParamValue = std::variant<int, float, double, bool, std::string>;

struct ParamRule {
    std::string  name;
    std::string  description;
    ParamValue   default_value;
    ParamValue   min_value;
    ParamValue   max_value;
    bool         requires_restart{false};
};

class RuntimeParamRules {
public:
    void register_rule(const std::string& param_name, ParamRule rule);
    bool is_modifiable(const std::string& param_name) const;
    bool validate(const std::string& param_name, const ParamValue& value) const;
    ParamValue get_default(const std::string& param_name) const;
    std::unordered_set<std::string> get_all_param_names() const;
    bool requires_restart(const std::string& param_name) const;
    static RuntimeParamRules create_default();

private:
    std::unordered_map<std::string, ParamRule> rules_;
};

} // namespace engine::domain
```

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
    if (it == rules_.end()) return true;
    return it->second.requires_restart;
}

RuntimeParamRules RuntimeParamRules::create_default() {
    RuntimeParamRules rules;
    rules.register_rule("confidence_threshold", {
        "confidence_threshold",
        "Minimum detection confidence (0.0 – 1.0)",
        0.5f, 0.0f, 1.0f, false
    });
    rules.register_rule("tracker_enabled", {
        "tracker_enabled",
        "Enable/disable object tracker",
        true, false, true, true
    });
    return rules;
}

} // namespace engine::domain
```

### Phase 4 — Build Integration

**GOAL-004**: Create `domain/CMakeLists.txt` defining `vms_engine_domain` static library.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-008 | Create `domain/CMakeLists.txt` with STATIC library target | ☐ | |

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

## 3. Alternatives

- **ALT-001**: Use `void*` instead of `std::any` for opaque metadata (rejected — `std::any` is type-safe and standard C++17).
- **ALT-002**: Put `DetectionResult`/`FrameEvent` in core layer (rejected — these are domain concepts, not framework interfaces).
- **ALT-003**: Implement metadata parsing in domain layer (rejected — parsing requires DeepStream SDK headers, violating domain purity).

---

## 4. Dependencies

- **DEP-001**: Plan 02 completed — `vms_engine_core` with config types.
- **DEP-002**: C++17 standard library — `std::any`, `std::variant`, `std::unordered_map`.
- **DEP-003**: No external dependencies beyond core.

---

## 5. Files

| ID | File Path | Description |
|----|-----------|-------------|
| FILE-001 | `domain/include/engine/domain/event_processor.hpp` | IEventProcessor + DetectionResult + FrameEvent |
| FILE-002 | `domain/include/engine/domain/metadata_parser.hpp` | IMetadataParser interface |
| FILE-003 | `domain/include/engine/domain/runtime_param_rules.hpp` | RuntimeParamRules registry |
| FILE-004 | `domain/src/runtime_param_rules.cpp` | RuntimeParamRules implementation + defaults |
| FILE-005 | `domain/CMakeLists.txt` | Build config for vms_engine_domain |

---

## 6. Testing & Verification

- **TEST-001**: Compile domain library — `cmake --build build --target vms_engine_domain -- -j5`.
- **TEST-002**: No infrastructure/pipeline/backend dependencies — `grep -r "infrastructure\|pipeline\|deepstream\|gst\|nvds" domain/ --include="*.hpp" --include="*.cpp" && echo "FAIL" || echo "PASS"`.
- **TEST-003**: Namespace consistency — `grep -rL "engine::domain" domain/include/ --include="*.hpp" | head -10`.
- **TEST-004**: `RuntimeParamRules::create_default()` returns valid rules with expected param names.
- **TEST-005**: `validate()` accepts correct types and rejects mismatched variant types.

---

## 7. Risks & Assumptions

- **RISK-001**: `std::any` runtime type erasure limits compile-time safety; mitigated by clear interface contracts and pipeline-layer type checks.
- **ASSUMPTION-001**: Actual `IEventProcessor` and `IMetadataParser` implementations live in `pipeline/` where DeepStream SDK is available.
- **ASSUMPTION-002**: Domain layer remains thin — heavy processing logic lives in pipeline probes and event handlers.
- **ASSUMPTION-003**: `ParamValue` variant covers all runtime-modifiable parameter types needed by the system.

---

## 8. Related Specifications

- [Plan 02 — Core Layer](02_core_layer.md) (prerequisite interfaces)
- [Plan 03 — Pipeline Layer](03_pipeline_layer.md) (implements domain interfaces)
- [Plan 05 — Infrastructure Layer](05_infrastructure_layer.md)
- [Event Handlers & Probes](../../docs/architecture/deepstream/07_event_handlers_probes.md)
- [AGENTS.md](../../AGENTS.md)
