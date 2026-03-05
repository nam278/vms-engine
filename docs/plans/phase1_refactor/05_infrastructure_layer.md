---
goal: "Plan 05 — Infrastructure Layer: Config Parser, Messaging, Storage, REST API"
version: "1.0"
date_created: "2025-01-15"
last_updated: "2025-07-17"
owner: "VMS Engine Team"
status: "Planned"
tags: [infrastructure, config-parser, yaml, messaging, redis, kafka, storage, rest-api, pistache]
---

# Plan 05 — Infrastructure Layer (Config Parser, Messaging, Storage, REST API)

![Status: Planned](https://img.shields.io/badge/status-Planned-blue)

Create all infrastructure files from scratch implementing core interfaces.
4 sub-modules with ~27 files total. Each sub-module implements a `core/` interface (Port → Adapter pattern).

---

## 1. Requirements & Constraints

- **REQ-001**: Plan 02 completed — core interfaces compile (`IConfigParser`, `IMessageProducer`, `IStorageManager`).
- **REQ-002**: Config parser produces flat `PipelineConfig` — no backend variants, no `std::variant` wrappers.
- **REQ-003**: All infrastructure implements core interfaces (Port → Adapter pattern).
- **REQ-004**: Config parser supports new YAML schema (`docs/configs/deepstream_default.yml`) — `queue_defaults:`, inline `queue: {}`, flat element properties.
- **REQ-005**: Each sub-module compiles independently as separate CMake target.
- **SEC-001**: Messaging adapters must validate connection strings; no hardcoded credentials.
- **CON-001**: All files use `engine::infrastructure::*` namespace.
- **CON-002**: Include paths follow `engine/infrastructure/<sub>/` convention.
- **CON-003**: No direct pipeline or domain layer imports.
- **GUD-001**: Use yaml-cpp for YAML parsing (FetchContent in root CMakeLists.txt).
- **GUD-002**: Use hiredis for Redis, libcurl for S3 storage.
- **PAT-001**: Port → Adapter — core defines interfaces, infrastructure provides implementations.

---

## 2. Implementation Steps

### Sub-Module 1 — Config Parser (~17 files, ~3,500+ lines)

The YAML config parser is the largest single component — 15 source files,
each parsing a different YAML section.
Implements `engine::core::config::IConfigParser`.

**GOAL-001**: Create `YamlConfigParser` class and 15 section parsers.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-001 | Create `yaml_config_parser.hpp` declaring `YamlConfigParser : IConfigParser` | ☐ | |
| TASK-002 | Create `yaml_parser_helpers.hpp` (src-local shared parsing utilities) | ☐ | |
| TASK-003 | Create `yaml_config_parser.cpp` entry point — `parse()`, dispatch to sub-parsers | ☐ | |
| TASK-004 | Create `yaml_parser_pipeline.cpp` — `pipeline:` section (id, name, log_level, gst_log_level, dot_file_dir) | ☐ | |
| TASK-005 | Create `yaml_parser_sources.cpp` — `sources:` section (cameras list, flat source options) | ☐ | |
| TASK-006 | Create `yaml_parser_recording.cpp` — `sources.smart_record*` fields | ☐ | |
| TASK-007 | Create `yaml_parser_queues.cpp` — `queue_defaults:` + inline `queue: {}` resolution (**new file**) | ☐ | |
| TASK-008 | Create `yaml_parser_processing.cpp` — `processing:` section (elements list) | ☐ | |
| TASK-009 | Create `yaml_parser_visuals.cpp` — `visuals:` section (elements list) | ☐ | |
| TASK-010 | Create `yaml_parser_outputs.cpp` — `outputs:` section (elements list) | ☐ | |
| TASK-011 | Create `yaml_parser_handlers.cpp` — `event_handlers:` section | ☐ | |
| TASK-012 | Create `yaml_parser_analytics.cpp` — `analytics:` section | ☐ | |
| TASK-013 | Create `yaml_parser_messaging.cpp` — `broker_configurations:` section | ☐ | |
| TASK-014 | Create `yaml_parser_storage.cpp` — `storage_configurations:` section | ☐ | |
| TASK-015 | Create `yaml_parser_api.cpp` — `rest_api:` section | ☐ | |
| TASK-016 | Create `yaml_parser_utils.cpp` — shared YAML node helpers | ☐ | |

#### Config Parser Files

| File | Description |
|------|-------------|
| `config_parser/include/.../config_parser/yaml_config_parser.hpp` | Declares `YamlConfigParser : IConfigParser` |
| `config_parser/src/yaml_parser_helpers.hpp` | Shared parsing utilities (src-local only) |
| `config_parser/src/yaml_config_parser.cpp` | Entry point: parse(), dispatch to sub-parsers |
| `config_parser/src/yaml_parser_analytics.cpp` | `analytics:` section |
| `config_parser/src/yaml_parser_api.cpp` | `rest_api:` section |
| `config_parser/src/yaml_parser_pipeline.cpp` | `pipeline:` section (id, name, log_level) |
| `config_parser/src/yaml_parser_handlers.cpp` | `event_handlers:` section |
| `config_parser/src/yaml_parser_messaging.cpp` | `broker_configurations:` section |
| `config_parser/src/yaml_parser_outputs.cpp` | `outputs:` section (elements list) |
| `config_parser/src/yaml_parser_processing.cpp` | `processing:` section (elements list) |
| `config_parser/src/yaml_parser_queues.cpp` | `queue_defaults:` + inline `queue: {}` resolution |
| `config_parser/src/yaml_parser_recording.cpp` | `sources.smart_record*` fields |
| `config_parser/src/yaml_parser_sources.cpp` | `sources:` section (cameras list) |
| `config_parser/src/yaml_parser_storage.cpp` | `storage_configurations:` section |
| `config_parser/src/yaml_parser_utils.cpp` | Shared YAML node helpers |
| `config_parser/src/yaml_parser_visuals.cpp` | `visuals:` section (elements list) |

**Total: 1 header + 1 helper header + 15 .cpp = 17 files**

#### Schema Changes (new YAML format)

The new config schema eliminates `backend_config.deepstream.*` nesting and reflects GStreamer topology directly:

| Old YAML key path | New YAML key path | Parser file affected |
|--------------------|-------------------|----------------------|
| `application.name` | `pipeline.id` / `pipeline.name` | `yaml_parser_application.cpp` → **rename to `yaml_parser_pipeline.cpp`** |
| `sources[].uris[]` + `sources[].source_names[]` | `sources.cameras[{name, uri}]` | `yaml_parser_sources.cpp` |
| `sources[].backend_options.deepstream.*` | `sources.*` (flat) | `yaml_parser_sources.cpp` |
| `sources[].backend_options.deepstream.smart_record*` | `sources.smart_record.*` | `yaml_parser_sources.cpp` |
| `processing_flow[].backend_config.deepstream.*` | `processing.elements[].*(flat)` | `yaml_parser_processing.cpp` |
| `processing_flow[].input_from` | removed — order is implicit in list | `yaml_parser_processing.cpp` |
| `visuals.tiler.backend_config.deepstream.*` | `visuals.elements[{type:tiler}].*` | `yaml_parser_visuals.cpp` |
| `visuals.osd.backend_config.deepstream.*` | `visuals.elements[{type:osd}].*` | `yaml_parser_visuals.cpp` |
| `outputs[].encoding.*` + `outputs[].destination.*` | `outputs[].elements[{type}].*` (flat) | `yaml_parser_outputs.cpp` |
| implicit queues via `QueueManager` heuristics | `queue: {}` inline per element + `queue_defaults:` | **new: `yaml_parser_queues.cpp`** |
| `event_handlers[].evidence_from` + `snapshot_type: N` magic | `event_handlers[].trigger` + `probe_element` | `yaml_parser_handlers.cpp` |

All other structural keys (`broker_configurations`, `storage_configurations`, `rest_api`) are unchanged.

---

### Sub-Module 2 — Messaging (Redis + Kafka)

**GOAL-002**: Create Redis and Kafka messaging adapters implementing `IMessageProducer`.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-017 | Create `redis_stream_producer.hpp` + `.cpp` — implements `IMessageProducer` via hiredis `XADD` | ☐ | |
| TASK-018 | Create `kafka_adapter.hpp` + `.cpp` — implements `IMessageProducer` via rdkafka | ☐ | |

| File | Description |
|------|-------------|
| `messaging/include/.../messaging/redis_stream_producer.hpp` | Implements `IMessageProducer`; Redis Streams via hiredis |
| `messaging/include/.../messaging/kafka_adapter.hpp` | Implements `IMessageProducer`; Kafka via rdkafka |
| `messaging/src/redis_stream_producer.cpp` | `XADD` to Redis stream |
| `messaging/src/kafka_adapter.cpp` | Kafka producer |

**Total: 2 headers + 2 sources = 4 files**

Both `RedisStreamProducer` and `KafkaAdapter` implement `engine::core::messaging::IMessageProducer`.
Instances created in `app/main.cpp` and injected into event handlers via constructor.

---

### Sub-Module 3 — Storage (Local + S3)

**GOAL-003**: Create local and S3 storage adapters implementing `IStorageManager`.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-019 | Create `local_storage_manager.hpp` + `.cpp` — `std::filesystem` + file I/O | ☐ | |
| TASK-020 | Create `s3_storage_manager.hpp` + `.cpp` — uploads to S3/MinIO via libcurl | ☐ | |

| File | Description |
|------|-------------|
| `storage/include/.../storage/local_storage_manager.hpp` | Implements `IStorageManager`; saves to filesystem |
| `storage/include/.../storage/s3_storage_manager.hpp` | Implements `IStorageManager`; uploads to S3/MinIO |
| `storage/src/local_storage_manager.cpp` | `std::filesystem` + file I/O |
| `storage/src/s3_storage_manager.cpp` | libcurl or AWS SDK |

**Total: 2 headers + 2 sources = 4 files**

---

### Sub-Module 4 — REST API

**GOAL-004**: Create Pistache HTTP server wrapping `IPipelineManager` for runtime control.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-021 | Create `pistache_server.hpp` + `.cpp` — REST server wrapping `IPipelineManager` | ☐ | |

| File | Description |
|------|-------------|
| `rest_api/include/.../rest_api/pistache_server.hpp` | REST server wrapping `IPipelineManager` |
| `rest_api/src/pistache_server.cpp` | Pistache HTTP handler implementation |

**Total: 1 header + 1 source = 2 files**

Provides runtime control (start/stop pipelines, update params). Uses `engine::core::pipeline::IPipelineManager` interface — no direct pipeline access.

---

### Build Integration

**GOAL-005**: Create `infrastructure/CMakeLists.txt` with separate targets per sub-module + unified INTERFACE target.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-022 | Create `infrastructure/CMakeLists.txt` with 4 STATIC + 1 INTERFACE targets | ☐ | |

```cmake
# infrastructure/CMakeLists.txt

# --- Config Parser ---
file(GLOB CONFIG_PARSER_SOURCES "config_parser/src/*.cpp")
add_library(vms_engine_config_parser STATIC ${CONFIG_PARSER_SOURCES})
target_include_directories(vms_engine_config_parser
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/config_parser/include
)
target_link_libraries(vms_engine_config_parser
    PUBLIC  vms_engine_core
    PRIVATE yaml-cpp
)

# --- Messaging ---
add_library(vms_engine_messaging STATIC
    messaging/src/redis_stream_producer.cpp
    messaging/src/kafka_adapter.cpp
)
target_include_directories(vms_engine_messaging
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/messaging/include
)
target_link_libraries(vms_engine_messaging
    PUBLIC  vms_engine_core
    PRIVATE hiredis::hiredis
)

# --- Storage ---
add_library(vms_engine_storage STATIC
    storage/src/local_storage_manager.cpp
    storage/src/s3_storage_manager.cpp
)
target_include_directories(vms_engine_storage
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/storage/include
)
target_link_libraries(vms_engine_storage
    PUBLIC  vms_engine_core
    PRIVATE CURL::libcurl
)

# --- REST API ---
add_library(vms_engine_rest_api STATIC
    rest_api/src/pistache_server.cpp
)
target_include_directories(vms_engine_rest_api
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/rest_api/include
)
target_link_libraries(vms_engine_rest_api
    PUBLIC  vms_engine_core
)

# --- Unified target ---
add_library(vms_engine_infrastructure INTERFACE)
target_link_libraries(vms_engine_infrastructure INTERFACE
    vms_engine_config_parser
    vms_engine_messaging
    vms_engine_storage
    vms_engine_rest_api
)
```

---

## 3. Alternatives

- **ALT-001**: Single monolithic infrastructure library (rejected — separate targets give faster incremental builds and independent testability).
- **ALT-002**: Use nlohmann/json instead of yaml-cpp (rejected — YAML is the established config format, JSON lacks readability for complex pipeline configs).
- **ALT-003**: Use gRPC instead of Pistache for REST API (rejected — Pistache is lightweight for simple runtime control; gRPC adds unnecessary complexity).

---

## 4. Dependencies

- **DEP-001**: Plan 02 completed — core interfaces (`IConfigParser`, `IMessageProducer`, `IStorageManager`).
- **DEP-002**: yaml-cpp 0.8.0 — FetchContent in root CMakeLists.txt.
- **DEP-003**: hiredis 1.3.0 — Redis Streams for messaging.
- **DEP-004**: libcurl — S3/MinIO HTTP uploads.
- **DEP-005**: Pistache — lightweight HTTP server for REST API.
- **DEP-006**: rdkafka (optional) — Kafka messaging adapter.

---

## 5. Files

### Include Path Convention

All infrastructure includes follow: `infrastructure/<sub>/include/engine/infrastructure/<sub>/`

Example: `engine/infrastructure/messaging/redis_stream_producer.hpp`

### File Count Summary

| Sub-Module | Headers | Src-local Headers | Sources | Total |
|------------|---------|-------------------|---------|-------|
| Config Parser | 1 | 1 | 15 | 17 |
| Messaging | 2 | 0 | 2 | 4 |
| Storage | 2 | 0 | 2 | 4 |
| REST API | 1 | 0 | 1 | 2 |
| **Total** | **6** | **1** | **20** | **27** |

| ID | File Path | Description |
|----|-----------|-------------|
| FILE-001 | `infrastructure/config_parser/include/.../yaml_config_parser.hpp` | YamlConfigParser : IConfigParser |
| FILE-002 | `infrastructure/config_parser/src/yaml_parser_helpers.hpp` | Shared parsing utilities (src-local) |
| FILE-003 | `infrastructure/config_parser/src/yaml_config_parser.cpp` | Entry point: parse(), dispatch |
| FILE-004 | `infrastructure/config_parser/src/yaml_parser_pipeline.cpp` | pipeline: section |
| FILE-005 | `infrastructure/config_parser/src/yaml_parser_sources.cpp` | sources: section (cameras list) |
| FILE-006 | `infrastructure/config_parser/src/yaml_parser_recording.cpp` | sources.smart_record* fields |
| FILE-007 | `infrastructure/config_parser/src/yaml_parser_queues.cpp` | queue_defaults: + inline queue: {} |
| FILE-008 | `infrastructure/config_parser/src/yaml_parser_processing.cpp` | processing: section |
| FILE-009 | `infrastructure/config_parser/src/yaml_parser_visuals.cpp` | visuals: section |
| FILE-010 | `infrastructure/config_parser/src/yaml_parser_outputs.cpp` | outputs: section |
| FILE-011 | `infrastructure/config_parser/src/yaml_parser_handlers.cpp` | event_handlers: section |
| FILE-012 | `infrastructure/config_parser/src/yaml_parser_analytics.cpp` | analytics: section |
| FILE-013 | `infrastructure/config_parser/src/yaml_parser_messaging.cpp` | broker_configurations: section |
| FILE-014 | `infrastructure/config_parser/src/yaml_parser_storage.cpp` | storage_configurations: section |
| FILE-015 | `infrastructure/config_parser/src/yaml_parser_api.cpp` | rest_api: section |
| FILE-016 | `infrastructure/config_parser/src/yaml_parser_utils.cpp` | Shared YAML node helpers |
| FILE-017 | `infrastructure/messaging/include/.../redis_stream_producer.hpp` | RedisStreamProducer : IMessageProducer |
| FILE-018 | `infrastructure/messaging/include/.../kafka_adapter.hpp` | KafkaAdapter : IMessageProducer |
| FILE-019 | `infrastructure/messaging/src/redis_stream_producer.cpp` | Redis XADD implementation |
| FILE-020 | `infrastructure/messaging/src/kafka_adapter.cpp` | Kafka producer implementation |
| FILE-021 | `infrastructure/storage/include/.../local_storage_manager.hpp` | LocalStorageManager : IStorageManager |
| FILE-022 | `infrastructure/storage/include/.../s3_storage_manager.hpp` | S3StorageManager : IStorageManager |
| FILE-023 | `infrastructure/storage/src/local_storage_manager.cpp` | std::filesystem + file I/O |
| FILE-024 | `infrastructure/storage/src/s3_storage_manager.cpp` | libcurl S3 upload |
| FILE-025 | `infrastructure/rest_api/include/.../pistache_server.hpp` | PistacheServer wrapping IPipelineManager |
| FILE-026 | `infrastructure/rest_api/src/pistache_server.cpp` | Pistache HTTP handler |
| FILE-027 | `infrastructure/CMakeLists.txt` | Build config for all sub-modules |

---

## 6. Testing & Verification

- **TEST-001**: Compile each sub-module independently — `cmake --build build --target vms_engine_config_parser -- -j5` (repeat for each target).
- **TEST-002**: No lantana references — `grep -r "lantana" infrastructure/ --include="*.hpp" --include="*.cpp" && echo "FAIL" || echo "PASS"`.
- **TEST-003**: Interface implementations present — `grep -r "IMessageProducer\|IStorageManager\|IConfigParser" infrastructure/*/include/ --include="*.hpp" | head -20`.
- **TEST-004**: New queue parser exists — `ls infrastructure/config_parser/src/yaml_parser_queues.cpp`.
- **TEST-005**: Unified INTERFACE target builds all sub-modules.
- **TEST-006**: Config parser outputs flat `PipelineConfig` (no backend variants, no `std::variant` wrappers).

---

## 7. Risks & Assumptions

- **RISK-001**: Config parser is ~3,500+ lines — highest risk of bugs; mitigated by separate .cpp per YAML section for isolated testing.
- **RISK-002**: Third-party library API changes (hiredis, rdkafka, Pistache); mitigated by wrapping behind core interfaces.
- **RISK-003**: S3 storage depends on network availability; mitigated by local storage fallback.
- **ASSUMPTION-001**: yaml-cpp 0.8.0 API is stable via FetchContent.
- **ASSUMPTION-002**: Pistache is sufficient for lightweight runtime control (no high-throughput HTTP workload).
- **ASSUMPTION-003**: All infrastructure adapters are created in `app/main.cpp` and injected via constructor.

---

## 8. Related Specifications

- [Plan 02 — Core Layer](02_core_layer.md) (defines `IConfigParser`, `IMessageProducer`, `IStorageManager`)
- [Plan 03 — Pipeline Layer](03_pipeline_layer.md) (consumes config parser output)
- [Plan 06 — Application Layer](06_services_app_layer.md) (wires infrastructure adapters)
- [Configuration System](../../docs/architecture/deepstream/05_configuration.md)
- [AGENTS.md](../../AGENTS.md)
