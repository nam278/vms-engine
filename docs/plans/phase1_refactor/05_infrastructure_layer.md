# Plan 05 — Infrastructure Layer (Config Parser, Messaging, Storage, REST API)

> Migrate all files from `lantanav2/infrastructure/` → `vms-engine/infrastructure/`.
> 4 sub-modules with ~26 files total (~3,500+ lines of real code).

---

## Prerequisites

- Plan 02 completed (core interfaces compile — `IConfigParser`, `IMessageProducer`, `IStorageManager`)

## Deliverables

- [ ] Config parser (1 header + 15 source files + 1 helper header) migrated
- [ ] Messaging adapters (2 headers + 2 sources) migrated
- [ ] Storage adapters (2 headers + 2 sources) migrated
- [ ] REST API server (1 header + 1 source) migrated
- [ ] All implement core interfaces (Port → Adapter pattern)
- [ ] `infrastructure/CMakeLists.txt` updated
- [ ] Infrastructure library compiles

---

## Sub-Module 1: Config Parser (~3,561 lines)

The YAML config parser is the largest single component — 15 source files parsing different YAML sections.

### File Migration Table

| Source (lantanav2)                                                | Target (vms-engine)                                                | Changes     |
| ----------------------------------------------------------------- | ------------------------------------------------------------------ | ------------ |
| **Header:**                                                       |                                                                    |              |
| `config_parser/include/.../config_parser/yaml_config_parser.hpp` | `config_parser/include/.../config_parser/yaml_config_parser.hpp`   | Namespace   |
| **Internal helper (src-local header):**                            |                                                                    |              |
| `config_parser/src/yaml_parser_helpers.hpp`                       | `config_parser/src/yaml_parser_helpers.hpp`                         | Namespace   |
| **Source files:**                                                  |                                                                    |              |
| `config_parser/src/yaml_config_parser.cpp` (184 lines)           | `config_parser/src/yaml_config_parser.cpp`                          | Namespace + includes |
| `config_parser/src/yaml_parser_analytics.cpp` (335 lines)        | `config_parser/src/yaml_parser_analytics.cpp`                       | Namespace + includes |
| `config_parser/src/yaml_parser_api.cpp` (53 lines)               | `config_parser/src/yaml_parser_api.cpp`                             | Namespace + includes |
| `config_parser/src/yaml_parser_application.cpp` (163 lines)      | `config_parser/src/yaml_parser_application.cpp`                     | Namespace + includes |
| `config_parser/src/yaml_parser_handlers.cpp` (258 lines)         | `config_parser/src/yaml_parser_handlers.cpp`                        | Namespace + includes |
| `config_parser/src/yaml_parser_messaging.cpp` (286 lines)        | `config_parser/src/yaml_parser_messaging.cpp`                       | Namespace + includes |
| `config_parser/src/yaml_parser_muxer.cpp` (144 lines)            | `config_parser/src/yaml_parser_muxer.cpp`                           | Namespace + includes |
| `config_parser/src/yaml_parser_outputs.cpp` (667 lines)          | `config_parser/src/yaml_parser_outputs.cpp`                         | Namespace + includes |
| `config_parser/src/yaml_parser_processing.cpp` (337 lines)       | `config_parser/src/yaml_parser_processing.cpp`                      | Namespace + includes |
| `config_parser/src/yaml_parser_recording.cpp` (194 lines)        | `config_parser/src/yaml_parser_recording.cpp`                       | Namespace + includes |
| `config_parser/src/yaml_parser_services.cpp` (116 lines)         | `config_parser/src/yaml_parser_services.cpp`                        | Namespace + includes |
| `config_parser/src/yaml_parser_sources.cpp` (294 lines)          | `config_parser/src/yaml_parser_sources.cpp`                         | Namespace + includes |
| `config_parser/src/yaml_parser_storage.cpp` (129 lines)          | `config_parser/src/yaml_parser_storage.cpp`                         | Namespace + includes |
| `config_parser/src/yaml_parser_utils.cpp` (95 lines)             | `config_parser/src/yaml_parser_utils.cpp`                           | Namespace + includes |
| `config_parser/src/yaml_parser_visuals.cpp` (306 lines)          | `config_parser/src/yaml_parser_visuals.cpp`                         | Namespace + includes |

**Total: 1 header + 1 helper header + 15 .cpp = 17 files**

#### Config Parser Clean-Up

1. **Remove backend variant parsing**: The parser currently reads `backend_type` from YAML and creates backend-specific configs. Simplify to produce a single config struct (no variant).
2. **Update include paths**: All includes from `lantana/core/config/` → `engine/core/config/`
3. **Remove DLStreamer parsing branches**: Any `if (backend == "dlstreamer")` blocks should be deleted.

#### Schema Changes (new YAML format → `docs/configs/deepstream_default.yml`)

The new config schema eliminates `backend_config.deepstream.*` nesting and
reflects GStreamer topology directly. Parser files affected:

| Old YAML key path | New YAML key path | Parser file affected |
| --- | --- | --- |
| `application.name` | `pipeline.id` / `pipeline.name` | `yaml_parser_application.cpp` → **rename to `yaml_parser_pipeline.cpp`** |
| `sources[].uris[]` + `sources[].source_names[]` | `sources.cameras[{name, uri}]` | `yaml_parser_sources.cpp` |
| `sources[].backend_options.deepstream.*` | `sources.*` (flat) | `yaml_parser_sources.cpp` |
| `sources[].backend_options.deepstream.smart_record*` | `sources.smart_record.*` | `yaml_parser_sources.cpp` |
| `processing_flow[].backend_config.deepstream.*` | `processing.elements[].*(flat)` | `yaml_parser_processing.cpp` |
| `processing_flow[].input_from` | removed — order is implicit in list | `yaml_parser_processing.cpp` |
| `visuals.tiler.backend_config.deepstream.*` | `visuals.elements[{type:tiler}].*` | `yaml_parser_visuals.cpp` |
| `visuals.osd.backend_config.deepstream.*` | `visuals.elements[{type:osd}].*` | `yaml_parser_visuals.cpp` |
| `outputs[].encoding.*` + `outputs[].destination.*` | `outputs[].elements[{type}].*` (flat element list, same pattern as processing/visuals) | `yaml_parser_outputs.cpp` |
| implicit queues via `QueueManager` heuristics | `queue: {}` inline per element + `queue_defaults:` | **new file: `yaml_parser_queues.cpp`** |
| `event_handlers[].evidence_from` + `snapshot_type: N` magic | `event_handlers[].trigger` + `probe_element` | `yaml_parser_handlers.cpp` |
| — | `queue_defaults:` top-level block | **new file: `yaml_parser_queues.cpp`** |

**New parser source file to add:**
```
config_parser/src/yaml_parser_queues.cpp   # parses queue_defaults + inline queue: {}
```

All other structural keys (`broker_configurations`, `storage_configurations`,
`rest_api`) are unchanged.

---

## Sub-Module 2: Messaging (Redis + Kafka)

### File Migration Table

| Source (lantanav2)                                                          | Target (vms-engine)                                                          | Changes              |
| --------------------------------------------------------------------------- | ---------------------------------------------------------------------------- | --------------------- |
| `messaging/include/.../messaging/redis_stream_producer.hpp`                | `messaging/include/.../messaging/redis_stream_producer.hpp`                  | Namespace; implement `IMessageProducer` |
| `messaging/include/.../messaging/kafka_adapter.hpp`                        | `messaging/include/.../messaging/kafka_adapter.hpp`                          | Namespace; implement `IMessageProducer` |
| `messaging/src/redis_stream_producer.cpp`                                   | `messaging/src/redis_stream_producer.cpp`                                     | Namespace + includes |
| `messaging/src/kafka_adapter.cpp`                                           | `messaging/src/kafka_adapter.cpp`                                             | Namespace + includes |

**Total: 2 headers + 2 sources = 4 files**

#### Messaging Clean-Up

1. **Implement `IMessageProducer` interface** (defined in Plan 02): Both `RedisStreamProducer` and `KafkaAdapter` must explicitly implement `engine::core::messaging::IMessageProducer`.
2. **Remove static registration patterns**: No global/static `shared_ptr` to Redis. Instances are created and injected by main.cpp.

---

## Sub-Module 3: Storage (Local + S3)

### File Migration Table

| Source (lantanav2)                                                      | Target (vms-engine)                                                      | Changes              |
| ----------------------------------------------------------------------- | ------------------------------------------------------------------------ | --------------------- |
| `storage/include/.../storage/local_storage_manager.hpp`                | `storage/include/.../storage/local_storage_manager.hpp`                  | Namespace; implement `IStorageManager` |
| `storage/include/.../storage/s3_storage_manager.hpp`                   | `storage/include/.../storage/s3_storage_manager.hpp`                     | Namespace; implement `IStorageManager` |
| `storage/src/local_storage_manager.cpp`                                 | `storage/src/local_storage_manager.cpp`                                   | Namespace + includes |
| `storage/src/s3_storage_manager.cpp`                                    | `storage/src/s3_storage_manager.cpp`                                      | Namespace + includes |

**Total: 2 headers + 2 sources = 4 files**

#### Storage Clean-Up

1. **Implement `IStorageManager` interface** (defined in Plan 02).
2. **Include paths**: `lantana/core/storage/` → `engine/core/storage/`

---

## Sub-Module 4: REST API

### File Migration Table

| Source (lantanav2)                                                      | Target (vms-engine)                                                      | Changes     |
| ----------------------------------------------------------------------- | ------------------------------------------------------------------------ | ------------ |
| `rest_api/include/.../rest_api/pistache_server.hpp`                    | `rest_api/include/.../rest_api/pistache_server.hpp`                      | Namespace   |
| `rest_api/src/pistache_server.cpp`                                      | `rest_api/src/pistache_server.cpp`                                        | Namespace + includes |

**Total: 1 header + 1 source = 2 files**

#### REST API Notes

- Pistache provides runtime control (start/stop pipelines, update params).
- Update endpoint handler to use `engine::core::pipeline::IPipelineManager` interface.
- Replace any direct DeepStream calls with interface methods.

---

## Include Path Mapping

All infrastructure files use this pattern:

```
lantanav2:    infrastructure/<sub>/include/lantana/infrastructure/<sub>/
vms-engine:   infrastructure/<sub>/include/engine/infrastructure/<sub>/
```

Example: `lantana/infrastructure/messaging/redis_stream_producer.hpp` → `engine/infrastructure/messaging/redis_stream_producer.hpp`

---

## Namespace Replacement Script

```bash
cd vms-engine/infrastructure

# Replace namespace
find . -name "*.hpp" -o -name "*.cpp" | xargs sed -i \
    -e 's/namespace lantana::infrastructure/namespace engine::infrastructure/g' \
    -e 's/namespace lantana/namespace engine/g' \
    -e 's/lantana::infrastructure::/engine::infrastructure::/g' \
    -e 's/lantana::/engine::/g'

# Replace include paths
find . -name "*.hpp" -o -name "*.cpp" | xargs sed -i \
    -e 's|#include "lantana/infrastructure/|#include "engine/infrastructure/|g' \
    -e 's|#include "lantana/core/|#include "engine/core/|g' \
    -e 's|#include "lantana/backends/deepstream/|#include "engine/pipeline/|g'

# Replace include guards
find . -name "*.hpp" | xargs sed -i \
    -e 's/LANTANA_INFRASTRUCTURE_/ENGINE_INFRASTRUCTURE_/g' \
    -e 's/LANTANA_/ENGINE_/g'
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

| Sub-Module     | Headers | Src-local Headers | Sources | Total |
| --------------- | ------- | ----------------- | ------- | ----- |
| Config Parser  | 1       | 1                 | 15      | 17    |
| Messaging      | 2       | 0                 | 2       | 4     |
| Storage        | 2       | 0                 | 2       | 4     |
| REST API       | 1       | 0                 | 1       | 2     |
| **Total**      | **6**   | **1**             | **20**  | **27** |

---

## Verification

```bash
# 1. Compile each sub-module independently
cmake --build build --target vms_engine_config_parser -- -j$(nproc)
cmake --build build --target vms_engine_messaging -- -j$(nproc)
cmake --build build --target vms_engine_storage -- -j$(nproc)
cmake --build build --target vms_engine_rest_api -- -j$(nproc)

# 2. Check no backend/lantana references
grep -r "lantana\|backends/deepstream" infrastructure/ && echo "FAIL" || echo "PASS"

# 3. Check interface implementations
grep -r "IMessageProducer\|IStorageManager\|IConfigParser" infrastructure/include/ | head -20

# 4. Check no DLStreamer references
grep -r "dlstreamer\|DLStreamer\|DLSTREAMER" infrastructure/ && echo "FAIL" || echo "PASS"
```

---

## Checklist

- [ ] Config parser: 17 files migrated, backend variant parsing removed
- [ ] Messaging: 4 files migrated, both implement `IMessageProducer`
- [ ] Storage: 4 files migrated, both implement `IStorageManager`
- [ ] REST API: 2 files migrated, uses `IPipelineManager` interface
- [ ] All `lantana::` → `engine::` namespace changes applied
- [ ] All include paths updated
- [ ] No remaining `lantana`, `backends/deepstream`, or `dlstreamer` references
- [ ] Each sub-module CMake target compiles independently
- [ ] Unified `vms_engine_infrastructure` INTERFACE target works
