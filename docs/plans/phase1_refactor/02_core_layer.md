# Plan 02 — Core Layer (Interfaces, Config Types, Utilities)

> Migrate all files from `lantanav2/core/` → `vms-engine/core/`.
> This is the foundation layer — **zero implementation details**, only interfaces + types + utils.

---

## Prerequisites

- Plan 01 completed (scaffold builds)

## Deliverables

- [ ] All 42 header files migrated under `core/include/engine/core/`
- [ ] All 3 source files migrated under `core/src/`
- [ ] Namespace `lantana::core::*` → `engine::core::*`
- [ ] Include paths `lantana/core/` → `engine/core/`
- [ ] Backend dependency leak in `ipipeline_manager.hpp` removed
- [ ] Redis coupling in `ihandler.hpp` removed
- [ ] `core/CMakeLists.txt` updated with all sources
- [ ] Core library compiles independently

---

## File Migration Table

### 2.1 — Builders (Interfaces)

| Source (lantanav2)                                            | Target (vms-engine)                                          | Changes           |
| ------------------------------------------------------------- | ------------------------------------------------------------ | ------------------ |
| `core/include/lantana/core/builders/ibuilder_factory.hpp`    | `core/include/engine/core/builders/ibuilder_factory.hpp`     | Namespace + include|
| `core/include/lantana/core/builders/ielement_builder.hpp`    | `core/include/engine/core/builders/ielement_builder.hpp`     | **Signature change** (see below)|
| `core/include/lantana/core/builders/ipipeline_builder.hpp`   | `core/include/engine/core/builders/ipipeline_builder.hpp`    | **Signature change** (see below)|

#### Builder Interface Signature Change (Full Config Pattern)

In lantanav2, builders receive a pre-sliced config section. In vms-engine, **all builders receive the full `PipelineConfig`** so they can freely cross-reference any section:

```cpp
// ielement_builder.hpp — BEFORE (lantanav2)
class IElementBuilder {
public:
    virtual ~IElementBuilder() = default;
    virtual GstElement* build(const std::any& config_section) = 0;
};

// ielement_builder.hpp — AFTER (vms-engine)
#include "engine/core/config/config_types.hpp"

class IElementBuilder {
public:
    virtual ~IElementBuilder() = default;
    // Full config always available; index selects which item in a repeated section
    virtual GstElement* build(const engine::core::config::PipelineConfig& config,
                              int index = 0) = 0;
};

// ipipeline_builder.hpp — AFTER (vms-engine)
class IPipelineBuilder {
public:
    virtual ~IPipelineBuilder() = default;
    virtual bool build(const engine::core::config::PipelineConfig& config,
                       GMainLoop* main_loop) = 0;
    virtual GstElement* get_pipeline() const = 0;
};
```

All concrete builders in `pipeline/block_builders/` and `pipeline/builders/` implement this signature.

### 2.2 — Config Types (Value Objects)

| Source (lantanav2)                                            | Target (vms-engine)                                          | Changes            |
| ------------------------------------------------------------- | ------------------------------------------------------------ | ------------------- |
| `core/include/lantana/core/config/config_types.hpp`          | `core/include/engine/core/config/config_types.hpp`           | Namespace; remove `ProcessingBackendOptions` variant |
| `core/include/lantana/core/config/source_config.hpp`         | `core/include/engine/core/config/source_config.hpp`          | Namespace; remove `SourceBackendOptionsVariant` |
| `core/include/lantana/core/config/muxer_config.hpp`          | `core/include/engine/core/config/muxer_config.hpp`           | Namespace          |
| `core/include/lantana/core/config/inference_config.hpp`      | `core/include/engine/core/config/inference_config.hpp`       | Namespace; flatten to single struct |
| `core/include/lantana/core/config/tracker_config.hpp`        | `core/include/engine/core/config/tracker_config.hpp`         | Namespace          |
| `core/include/lantana/core/config/tiler_config.hpp`          | `core/include/engine/core/config/tiler_config.hpp`           | Namespace          |
| `core/include/lantana/core/config/osd_config.hpp`            | `core/include/engine/core/config/osd_config.hpp`             | Namespace          |
| `core/include/lantana/core/config/sink_config.hpp`           | `core/include/engine/core/config/sink_config.hpp`            | Namespace          |
| `core/include/lantana/core/config/encoding_config.hpp`       | `core/include/engine/core/config/encoding_config.hpp`        | Namespace          |
| `core/include/lantana/core/config/iconfig_parser.hpp`        | `core/include/engine/core/config/iconfig_parser.hpp`         | Namespace          |
| `core/include/lantana/core/config/iconfig_validator.hpp`     | `core/include/engine/core/config/iconfig_validator.hpp`      | Namespace          |

### 2.3 — Pipeline Interfaces

| Source (lantanav2)                                            | Target (vms-engine)                                          | Changes            |
| ------------------------------------------------------------- | ------------------------------------------------------------ | ------------------- |
| `core/include/lantana/core/pipeline/ipipeline_manager.hpp`   | `core/include/engine/core/pipeline/ipipeline_manager.hpp`    | **Critical**: Remove `#include "lantana/backends/deepstream/..."` |
| `core/include/lantana/core/pipeline/pipeline_state.hpp`      | `core/include/engine/core/pipeline/pipeline_state.hpp`       | Namespace          |
| `core/include/lantana/core/pipeline/pipeline_info.hpp`       | `core/include/engine/core/pipeline/pipeline_info.hpp`        | Namespace          |

### 2.4 — Eventing Interfaces

| Source (lantanav2)                                                | Target (vms-engine)                                               | Changes     |
| ----------------------------------------------------------------- | ----------------------------------------------------------------- | ------------ |
| `core/include/lantana/core/eventing/ievent_handler.hpp`          | `core/include/engine/core/eventing/ievent_handler.hpp`            | Namespace   |
| `core/include/lantana/core/eventing/ievent_manager.hpp`          | `core/include/engine/core/eventing/ievent_manager.hpp`            | Fill if empty|
| `core/include/lantana/core/eventing/ievent_listener.hpp`         | `core/include/engine/core/eventing/ievent_listener.hpp`           | Fill if empty|
| `core/include/lantana/core/eventing/event_types.hpp`             | `core/include/engine/core/eventing/event_types.hpp`               | Namespace   |
| `core/include/lantana/core/eventing/handler_register_macro.hpp`  | `core/include/engine/core/eventing/handler_register_macro.hpp`    | Namespace   |
| `core/include/lantana/core/eventing/handler_registry.hpp`        | `core/include/engine/core/eventing/handler_registry.hpp`          | Namespace   |

### 2.5 — Handler Interfaces

| Source (lantanav2)                                                | Target (vms-engine)                                               | Changes       |
| ----------------------------------------------------------------- | ----------------------------------------------------------------- | -------------- |
| `core/include/lantana/core/handlers/ihandler.hpp`                | `core/include/engine/core/handlers/ihandler.hpp`                  | **Critical**: Remove static `redis_producer_` |
| `core/include/lantana/core/handlers/handler_registry.hpp`        | `core/include/engine/core/handlers/handler_registry.hpp`          | Namespace     |
| `core/include/lantana/core/handlers/handler_register_macro.hpp`  | `core/include/engine/core/handlers/handler_register_macro.hpp`    | Namespace     |

### 2.6 — Probe Interfaces

| Source (lantanav2)                                        | Target (vms-engine)                                        | Changes   |
| --------------------------------------------------------- | ---------------------------------------------------------- | ---------- |
| `core/include/lantana/core/probes/iprobe_handler.hpp`    | `core/include/engine/core/probes/iprobe_handler.hpp`       | Namespace |

### 2.7 — Messaging Interfaces

| Source (lantanav2)                                           | Target (vms-engine)                                           | Changes     |
| ------------------------------------------------------------ | ------------------------------------------------------------- | ------------ |
| `core/include/lantana/core/messaging/imessage_producer.hpp` | `core/include/engine/core/messaging/imessage_producer.hpp`    | Fill if empty|
| `core/include/lantana/core/messaging/imessage_consumer.hpp` | `core/include/engine/core/messaging/imessage_consumer.hpp`    | Namespace   |

### 2.8 — Storage Interfaces

| Source (lantanav2)                                          | Target (vms-engine)                                          | Changes     |
| ----------------------------------------------------------- | ------------------------------------------------------------ | ------------ |
| `core/include/lantana/core/storage/istorage_manager.hpp`   | `core/include/engine/core/storage/istorage_manager.hpp`      | Fill if empty|
| `core/include/lantana/core/storage/storage_types.hpp`      | `core/include/engine/core/storage/storage_types.hpp`         | Namespace   |

### 2.9 — Recording Interfaces

| Source (lantanav2)                                                  | Target (vms-engine)                                                  | Changes     |
| ------------------------------------------------------------------- | -------------------------------------------------------------------- | ------------ |
| `core/include/lantana/core/recording/ismart_record_controller.hpp` | `core/include/engine/core/recording/ismart_record_controller.hpp`    | Fill if empty|
| `core/include/lantana/core/recording/recording_status.hpp`         | `core/include/engine/core/recording/recording_status.hpp`            | Namespace   |

### 2.10 — Runtime Interfaces

| Source (lantanav2)                                                    | Target (vms-engine)                                                    | Changes   |
| --------------------------------------------------------------------- | ---------------------------------------------------------------------- | ---------- |
| `core/include/lantana/core/runtime/iruntime_param_manager.hpp`      | `core/include/engine/core/runtime/iruntime_param_manager.hpp`          | Namespace |
| `core/include/lantana/core/runtime/iruntime_stream_manager.hpp`     | `core/include/engine/core/runtime/iruntime_stream_manager.hpp`         | Namespace |

### 2.11 — Services Interfaces

| Source (lantanav2)                                                       | Target (vms-engine)                                                       | Changes   |
| ------------------------------------------------------------------------ | ------------------------------------------------------------------------- | ---------- |
| `core/include/lantana/core/services/iexternal_inference_client.hpp`     | `core/include/engine/core/services/iexternal_inference_client.hpp`        | Namespace |

### 2.12 — Utilities

| Source (lantanav2)                                          | Target (vms-engine)                                          | Changes   |
| ----------------------------------------------------------- | ------------------------------------------------------------ | ---------- |
| `core/include/lantana/core/utils/logger.hpp`               | `core/include/engine/core/utils/logger.hpp`                  | Namespace |
| `core/include/lantana/core/utils/spdlog_logger.hpp`        | `core/include/engine/core/utils/spdlog_logger.hpp`           | Namespace |
| `core/include/lantana/core/utils/thread_safe_queue.hpp`    | `core/include/engine/core/utils/thread_safe_queue.hpp`       | Namespace |
| `core/include/lantana/core/utils/uuid_v7_generator.hpp`    | `core/include/engine/core/utils/uuid_v7_generator.hpp`       | Namespace |
| `core/src/utils/logger.cpp`                                 | `core/src/utils/logger.cpp`                                   | Include path|
| `core/src/utils/spdlog_logger.cpp`                          | `core/src/utils/spdlog_logger.cpp`                            | Include path|
| `core/src/utils/uuid_v7_generator.cpp`                      | `core/src/utils/uuid_v7_generator.cpp`                        | Include path|

---

## Critical Fixes During Migration

### Fix 1: Remove Backend Leak from IPipelineManager

**Problem:** `ipipeline_manager.hpp` includes a DeepStream-specific header:
```cpp
// WRONG — core depends on backend
#include "lantana/backends/deepstream/block_builders/pipeline_builder.hpp"
```

**Fix:** Remove backend include. If `IPipelineManager::initialize()` needs `IPipelineBuilder`, use forward declaration or include the core interface:
```cpp
// CORRECT — core only depends on itself
#include "engine/core/builders/ipipeline_builder.hpp"
```

Review the method signature and change any DeepStream-specific type to the core interface type.

### Fix 2: Remove Redis Coupling from IHandler

**Problem:** `ihandler.hpp` has static member of infrastructure type:
```cpp
class IHandler {
protected:
    static std::shared_ptr<RedisStreamProducer> redis_producer_;  // WRONG
    // ...
};
```

**Fix:** Remove the static Redis member from the base interface. Instead, inject `IMessageProducer` into concrete handler constructors:
```cpp
class IHandler {
public:
    virtual ~IHandler() = default;
    virtual bool init_context(const std::string& config) = 0;
    virtual bool init_context(const std::string& config, const std::any& extra) {
        return init_context(config);
    }
    virtual void destroy_context() = 0;
    virtual void register_callback() = 0;
    virtual std::string get_handler_name() const = 0;
    virtual std::vector<std::string> get_supported_event_types() const = 0;
    // NO static members, NO infrastructure types
};
```

Concrete handlers in `pipeline/event_handlers/` will receive `IMessageProducer*` through constructor injection.

### Fix 3: Fill Empty Interface Files

Several core interfaces are empty placeholders in lantanav2. Fill them with proper interface definitions:

**`imessage_producer.hpp`** (currently empty):
```cpp
namespace engine::core::messaging {
class IMessageProducer {
public:
    virtual ~IMessageProducer() = default;
    virtual bool connect(const std::string& host, int port, const std::string& channel = "") = 0;
    virtual bool publish(const std::string& channel, const std::string& message) = 0;
    virtual bool publish(const std::string& channel, const std::string& key,
                         const std::string& value) = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;
};
}
```

**`istorage_manager.hpp`** (currently empty):
```cpp
namespace engine::core::storage {
class IStorageManager {
public:
    virtual ~IStorageManager() = default;
    virtual bool initialize(const StorageTargetConfig& config) = 0;
    virtual bool save(const std::string& dest_path, const void* data, size_t size) = 0;
    virtual bool save_file(const std::string& src_path, const std::string& dest_path) = 0;
    virtual std::string get_url(const std::string& path, int expires_hours = 24) = 0;
    virtual bool remove(const std::string& path) = 0;
};
}
```

**`ievent_manager.hpp`** (currently empty):
```cpp
namespace engine::core::events {
class IEventManager {
public:
    virtual ~IEventManager() = default;
    virtual void register_handler(std::unique_ptr<IEventHandler> handler) = 0;
    virtual void unregister_handler(const std::string& handler_name) = 0;
    virtual void dispatch_event(const std::string& event_type, const std::any& data) = 0;
    virtual void shutdown() = 0;
};
}
```

**`ismart_record_controller.hpp`** (currently empty):
```cpp
namespace engine::core::recording {
class ISmartRecordController {
public:
    virtual ~ISmartRecordController() = default;
    virtual bool initialize(GstElement* pipeline, const SmartRecordConfig& config) = 0;
    virtual bool start_recording(int source_id, int duration_sec = 0) = 0;
    virtual bool stop_recording(int source_id) = 0;
    virtual RecordingStatus get_status(int source_id) const = 0;
};
}
```

---

## Config Type Simplification

### Remove `std::variant` Wrappers

In lantanav2, config types use variants to support multiple backends:

```cpp
// BEFORE (lantanav2) — supports DS + DL
using ProcessingBackendOptions = std::variant<
    deepstream::DeepStreamProcessingConfig,
    dlstreamer::DLStreamerProcessingConfig
>;
```

Replace with direct struct:

```cpp
// AFTER (vms-engine) — DeepStream only
struct InferenceBackendConfig {
    std::optional<std::string> model_config_path;
    std::optional<int> unique_id;
    std::optional<int> gpu_id;
    std::optional<int> batch_size;
    std::optional<std::string> tracker_lib;
    std::optional<std::string> config_path;
    std::optional<int> operate_on_gie_id;
    std::optional<std::vector<int>> operate_on_class_ids;
    std::optional<YAML::Node> properties;
};
```

Similarly for `SourceBackendOptions`, `MuxerBackendConfig`, `SinkBackendConfig`, etc.

---

## Namespace Replacement Script

For bulk replacement across all copied files:

```bash
cd vms-engine/core

# Replace namespace declarations
find . -name "*.hpp" -o -name "*.cpp" | xargs sed -i \
    -e 's/namespace lantana/namespace engine/g' \
    -e 's/lantana::/engine::/g'

# Replace include paths
find . -name "*.hpp" -o -name "*.cpp" | xargs sed -i \
    -e 's|#include "lantana/core/|#include "engine/core/|g' \
    -e 's|#include "lantana/backends/deepstream/|#include "engine/pipeline/|g' \
    -e 's|#include "lantana/infrastructure/|#include "engine/infrastructure/|g' \
    -e 's|#include "lantana/domain/|#include "engine/domain/|g' \
    -e 's|#include "lantana/services/|#include "engine/services/|g'

# Replace include guards
find . -name "*.hpp" | xargs sed -i \
    -e 's/LANTANA_CORE_/ENGINE_CORE_/g' \
    -e 's/LANTANA_/ENGINE_/g'
```

---

## CMakeLists.txt Update

```cmake
# core/CMakeLists.txt
add_library(vms_engine_core STATIC
    src/utils/logger.cpp
    src/utils/spdlog_logger.cpp
    src/utils/uuid_v7_generator.cpp
)

target_include_directories(vms_engine_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PUBLIC ${DEEPSTREAM_DIR}/sources/includes
)

target_link_libraries(vms_engine_core
    PUBLIC spdlog::spdlog
    PUBLIC PkgConfig::GLIB2
    PUBLIC PkgConfig::GST
)
```

---

## Verification

```bash
# 1. Compile core library alone
cmake --build build --target vms_engine_core -- -j$(nproc)

# 2. Check no backend dependencies
grep -r "backends\|deepstream\|dlstreamer" core/include/ core/src/ && echo "FAIL" || echo "PASS"

# 3. Check no lantana references
grep -r "lantana" core/include/ core/src/ && echo "FAIL" || echo "PASS"

# 4. Check all headers have engine:: namespace
grep -rL "engine::" core/include/engine/ | head -20
```

---

## Checklist

- [ ] All 42 headers migrated to `core/include/engine/core/`
- [ ] All 3 source files migrated to `core/src/`
- [ ] `lantana::` → `engine::` in all files
- [ ] `lantana/core/` → `engine/core/` in all includes
- [ ] `ipipeline_manager.hpp` — no backend includes
- [ ] `ihandler.hpp` — no static Redis member
- [ ] Empty interfaces filled: `imessage_producer`, `istorage_manager`, `ievent_manager`, `ismart_record_controller`
- [ ] Config types simplified (no `std::variant` wrappers)
- [ ] `core/CMakeLists.txt` updated
- [ ] `vms_engine_core` library compiles
- [ ] Zero `lantana` references in core/
