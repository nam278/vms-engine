# Plan 05 — Infrastructure Layer (Config Parser, Messaging, Storage, REST API)

> Create all infrastructure files from scratch implementing core interfaces.
> 4 sub-modules with ~26 files total. Each sub-module implements a `core/` interface (Port → Adapter pattern).

---

## Prerequisites

- Plan 02 completed (core interfaces compile — `IConfigParser`, `IMessageProducer`, `IStorageManager`)

## Deliverables

- [ ] Config parser (1 header + 15 source files + 1 helper header) created
- [ ] Messaging adapters (2 headers + 2 sources) created
- [ ] Storage adapters (2 headers + 2 sources) created
- [ ] REST API server (1 header + 1 source) created
- [ ] All implement core interfaces (Port → Adapter pattern)
- [ ] `infrastructure/CMakeLists.txt` created
- [ ] Infrastructure library compiles

---

## Sub-Module 1: Config Parser (~3,500+ lines)

The YAML config parser is the largest single component — 15 source files, each parsing a different YAML section.
Implements `engine::core::config::IConfigParser`.

### Files to Create

| File                                                             | Description                                       |
| ---------------------------------------------------------------- | ------------------------------------------------- |
| **Header:**                                                      |                                                   |
| `config_parser/include/.../config_parser/yaml_config_parser.hpp` | Declares `YamlConfigParser : IConfigParser`       |
| **Internal helper (src-local):**                                 |                                                   |
| `config_parser/src/yaml_parser_helpers.hpp`                      | Shared parsing utilities (src-local only)         |
| **Source files:**                                                |                                                   |
| `config_parser/src/yaml_config_parser.cpp`                       | Entry point: parse(), dispatch to sub-parsers     |
| `config_parser/src/yaml_parser_analytics.cpp`                    | `analytics:` section                              |
| `config_parser/src/yaml_parser_api.cpp`                          | `rest_api:` section                               |
| `config_parser/src/yaml_parser_pipeline.cpp`                     | `pipeline:` section (id, name, log_level)         |
| `config_parser/src/yaml_parser_handlers.cpp`                     | `event_handlers:` section                         |
| `config_parser/src/yaml_parser_messaging.cpp`                    | `broker_configurations:` section                  |
| `config_parser/src/yaml_parser_outputs.cpp`                      | `outputs:` section (elements list)                |
| `config_parser/src/yaml_parser_processing.cpp`                   | `processing:` section (elements list)             |
| `config_parser/src/yaml_parser_queues.cpp`                       | `queue_defaults:` + inline `queue: {}` resolution |
| `config_parser/src/yaml_parser_recording.cpp`                    | `sources.smart_record*` fields                    |
| `config_parser/src/yaml_parser_services.cpp`                     | `services:` section (Triton)                      |
| `config_parser/src/yaml_parser_sources.cpp`                      | `sources:` section (cameras list)                 |
| `config_parser/src/yaml_parser_storage.cpp`                      | `storage_configurations:` section                 |
| `config_parser/src/yaml_parser_utils.cpp`                        | Shared YAML node helpers                          |
| `config_parser/src/yaml_parser_visuals.cpp`                      | `visuals:` section (elements list)                |

**Total: 1 header + 1 helper header + 15 .cpp = 17 files**

#### Config Parser Notes

1. **All output is `PipelineConfig`**: The parser produces a flat, DeepStream-native `PipelineConfig` struct with no backend variants.
2. **`queue_defaults:` support**: `yaml_parser_queues.cpp` reads the top-level `queue_defaults:` block and resolves inline `queue: {}` entries on each element.
3. **New file**: `yaml_parser_queues.cpp` is new (no equivalent in older code).
4. **`yaml_parser_pipeline.cpp`**: Parses `pipeline:` section (id, name, log_level, gst_log_level, dot_file_dir).

#### Schema Changes (new YAML format → `docs/configs/deepstream_default.yml`)

The new config schema eliminates `backend_config.deepstream.*` nesting and
reflects GStreamer topology directly. Parser files affected:

| Old YAML key path                                           | New YAML key path                                                                      | Parser file affected                                                     |
| ----------------------------------------------------------- | -------------------------------------------------------------------------------------- | ------------------------------------------------------------------------ |
| `application.name`                                          | `pipeline.id` / `pipeline.name`                                                        | `yaml_parser_application.cpp` → **rename to `yaml_parser_pipeline.cpp`** |
| `sources[].uris[]` + `sources[].source_names[]`             | `sources.cameras[{name, uri}]`                                                         | `yaml_parser_sources.cpp`                                                |
| `sources[].backend_options.deepstream.*`                    | `sources.*` (flat)                                                                     | `yaml_parser_sources.cpp`                                                |
| `sources[].backend_options.deepstream.smart_record*`        | `sources.smart_record.*`                                                               | `yaml_parser_sources.cpp`                                                |
| `processing_flow[].backend_config.deepstream.*`             | `processing.elements[].*(flat)`                                                        | `yaml_parser_processing.cpp`                                             |
| `processing_flow[].input_from`                              | removed — order is implicit in list                                                    | `yaml_parser_processing.cpp`                                             |
| `visuals.tiler.backend_config.deepstream.*`                 | `visuals.elements[{type:tiler}].*`                                                     | `yaml_parser_visuals.cpp`                                                |
| `visuals.osd.backend_config.deepstream.*`                   | `visuals.elements[{type:osd}].*`                                                       | `yaml_parser_visuals.cpp`                                                |
| `outputs[].encoding.*` + `outputs[].destination.*`          | `outputs[].elements[{type}].*` (flat element list, same pattern as processing/visuals) | `yaml_parser_outputs.cpp`                                                |
| implicit queues via `QueueManager` heuristics               | `queue: {}` inline per element + `queue_defaults:`                                     | **new file: `yaml_parser_queues.cpp`**                                   |
| `event_handlers[].evidence_from` + `snapshot_type: N` magic | `event_handlers[].trigger` + `probe_element`                                           | `yaml_parser_handlers.cpp`                                               |
| —                                                           | `queue_defaults:` top-level block                                                      | **new file: `yaml_parser_queues.cpp`**                                   |

**New parser source file to add:**

```
config_parser/src/yaml_parser_queues.cpp   # parses queue_defaults + inline queue: {}
```

All other structural keys (`broker_configurations`, `storage_configurations`,
`rest_api`) are unchanged.

---

## Sub-Module 2: Messaging (Redis + Kafka)

### Files to Create

| File                                                        | Description                                              |
| ----------------------------------------------------------- | -------------------------------------------------------- |
| `messaging/include/.../messaging/redis_stream_producer.hpp` | Implements `IMessageProducer`; Redis Streams via hiredis |
| `messaging/include/.../messaging/kafka_adapter.hpp`         | Implements `IMessageProducer`; Kafka via rdkafka         |
| `messaging/src/redis_stream_producer.cpp`                   | `XADD` to Redis stream                                   |
| `messaging/src/kafka_adapter.cpp`                           | Kafka producer                                           |

**Total: 2 headers + 2 sources = 4 files**

#### Messaging Notes

1. Both `RedisStreamProducer` and `KafkaAdapter` implement `engine::core::messaging::IMessageProducer`.
2. Instances are created in `app/main.cpp` and injected into event handlers via constructor.

---

## Sub-Module 3: Storage (Local + S3)

### Files to Create

| File                                                    | Description                                       |
| ------------------------------------------------------- | ------------------------------------------------- |
| `storage/include/.../storage/local_storage_manager.hpp` | Implements `IStorageManager`; saves to filesystem |
| `storage/include/.../storage/s3_storage_manager.hpp`    | Implements `IStorageManager`; uploads to S3/MinIO |
| `storage/src/local_storage_manager.cpp`                 | `std::filesystem` + file I/O                      |
| `storage/src/s3_storage_manager.cpp`                    | libcurl or AWS SDK                                |

**Total: 2 headers + 2 sources = 4 files**

#### Storage Notes

1. Both implement `engine::core::storage::IStorageManager`.

---

## Sub-Module 4: REST API

### Files to Create

| File                                                | Description                             |
| --------------------------------------------------- | --------------------------------------- |
| `rest_api/include/.../rest_api/pistache_server.hpp` | REST server wrapping `IPipelineManager` |
| `rest_api/src/pistache_server.cpp`                  | Pistache HTTP handler implementation    |

**Total: 1 header + 1 source = 2 files**

#### REST API Notes

- Pistache provides runtime control (start/stop pipelines, update params).
- Uses `engine::core::pipeline::IPipelineManager` interface — no direct pipeline access.

---

## Include Path Conventions

All infrastructure includes follow this pattern:

```
infrastructure/<sub>/include/engine/infrastructure/<sub>/
```

Example: `engine/infrastructure/messaging/redis_stream_producer.hpp`

All internal includes reference core interfaces:

```cpp
#include "engine/core/messaging/imessage_producer.hpp"
#include "engine/core/config/config_types.hpp"
```

---

## CMakeLists.txt

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
    # PRIVATE rdkafka   # Only if Kafka is linked
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
    # PRIVATE pistache   # or PkgConfig::PISTACHE
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

## File Count Summary

| Sub-Module    | Headers | Src-local Headers | Sources | Total  |
| ------------- | ------- | ----------------- | ------- | ------ |
| Config Parser | 1       | 1                 | 15      | 17     |
| Messaging     | 2       | 0                 | 2       | 4      |
| Storage       | 2       | 0                 | 2       | 4      |
| REST API      | 1       | 0                 | 1       | 2      |
| **Total**     | **6**   | **1**             | **20**  | **27** |

---

## Verification

```bash
# Inside container: docker compose exec app bash
cd /opt/vms_engine

# 1. Compile each sub-module independently
cmake --build build --target vms_engine_config_parser -- -j5
cmake --build build --target vms_engine_messaging -- -j5
cmake --build build --target vms_engine_storage -- -j5
cmake --build build --target vms_engine_rest_api -- -j5

# 2. Check no lantana references
grep -r "lantana" infrastructure/ --include="*.hpp" --include="*.cpp" \
    && echo "FAIL" || echo "PASS"

# 3. Check interface implementations
grep -r "IMessageProducer\|IStorageManager\|IConfigParser" \
    infrastructure/*/include/ --include="*.hpp" | head -20

# 4. Check new queue parser exists
ls infrastructure/config_parser/src/yaml_parser_queues.cpp
```

---

## Checklist

- [ ] Config parser: 17 files created, outputs flat `PipelineConfig` (no backend variants)
- [ ] `yaml_parser_queues.cpp` created: handles `queue_defaults:` + inline `queue: {}` resolution
- [ ] `yaml_parser_pipeline.cpp` created: parses `pipeline:` section
- [ ] Messaging: 4 files created, both implement `IMessageProducer`
- [ ] Storage: 4 files created, both implement `IStorageManager`
- [ ] REST API: 2 files created, uses `IPipelineManager` interface
- [ ] All files in `engine::infrastructure::*` namespace
- [ ] All includes use `"engine/infrastructure/..."` and `"engine/core/..."` paths
- [ ] Each sub-module CMake target compiles independently
- [ ] Unified `vms_engine_infrastructure` INTERFACE target works
