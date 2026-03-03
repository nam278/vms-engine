# Plan 06 — Services, Application Entry Point, and Plugins

> Create external services (`services/`), write `app/main.cpp`, and create `plugins/`.
> This is the final implementation plan before integration testing.

---

## Prerequisites

- Plans 02–05 completed (all libraries compile independently)

## Deliverables

- [ ] Triton inference client (1 header + 1 source) created
- [ ] `app/main.cpp` written (DeepStream-only, `engine::` namespace throughout)
- [ ] All 7 plugin source files created
- [ ] Final binary links and compiles

---

## Part A: External Services (Triton Client)

### Files to Create

| File                                                           | Purpose                                                                 |
| -------------------------------------------------------------- | ----------------------------------------------------------------------- |
| `services/include/engine/services/triton_inference_client.hpp` | Declares `TritonInferenceClient`; implements `IExternalInferenceClient` |
| `services/src/triton_inference_client.cpp`                     | HTTP/gRPC calls to Triton Inference Server                              |

### Conventions

- Namespace: `engine::services`
- Implement `engine::core::services::IExternalInferenceClient`
- Includes: `#include "engine/core/services/..."`

### CMakeLists.txt

```cmake
# services/CMakeLists.txt
add_library(vms_engine_services STATIC
    src/triton_inference_client.cpp
)

target_include_directories(vms_engine_services
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(vms_engine_services
    PUBLIC  vms_engine_core
    PRIVATE CURL::libcurl    # or gRPC if Triton uses gRPC
)
```

---

## Part B: Application Entry Point (main.cpp)

`app/main.cpp` is the single entry point (~450 lines). It is DeepStream-only — no `#ifdef` guards, no backend variants.

### Design Decisions

1. **DeepStream-only** — No `#ifdef LANTANA_WITH_DEEPSTREAM` guards; DeepStream is the only pipeline backend.
2. **Direct construction** — `create_pipeline_manager()` calls `std::make_shared<engine::pipeline::PipelineManager>()` directly.
3. **Flat config** — Parser returns `PipelineConfig` directly; no `std::variant` wrapping.
4. **Logger name**: `"vms_engine"`
5. **Default config path**: `-c configs/default.yml`

### Rewritten Structure (Pseudocode)

```cpp
// app/main.cpp

// Core includes
#include "engine/core/config/config_types.hpp"
#include "engine/core/config/iconfig_parser.hpp"
#include "engine/core/pipeline/ipipeline_manager.hpp"
#include "engine/core/utils/logger.hpp"

// Infrastructure includes
#include "engine/infrastructure/config_parser/yaml_config_parser.hpp"

// Pipeline includes (no #ifdef guard!)
#include "engine/pipeline/pipeline_manager.hpp"

// GStreamer/GLib
#include <glib.h>
#include <gst/gst.h>

// Standard library
#include <atomic>
#include <csignal>
#include <memory>
#include <string>
// ... (same as before minus <variant>)

namespace {

GMainLoop* g_main_loop = nullptr;
std::shared_ptr<engine::core::pipeline::IPipelineManager> g_pipeline_manager = nullptr;
std::atomic<bool> g_stop_called{false};

void handle_signal(int signum) { /* Same logic, updated namespace */ }
void handle_crash_signal(int signum) { /* Same logic — unchanged */ }

std::string parse_arguments(int argc, char* argv[]) { /* Same logic */ }

bool initialize_logger(const engine::core::config::PipelineConfig* config,
                       const std::string& error_msg) {
    // Same logic, namespace: engine::core::utils::
    // Logger name: "vms_engine"
}

// SIMPLIFIED: No backend_type switch, no #ifdef
std::shared_ptr<engine::core::pipeline::IPipelineManager> create_pipeline_manager() {
    return std::make_shared<engine::pipeline::PipelineManager>();
}

}  // anonymous namespace

int main(int argc, char* argv[]) {
    // Steps 1-11 same structure, updated namespaces:
    // 1. parse_arguments()
    // 2. YamlConfigParser::parse()
    // 3. initialize_logger()
    // 4. Log config summary
    // 5. Create GMainLoop
    // 6. create_pipeline_manager() + initialize(config, g_main_loop)
    // 7. Register signal handlers
    // 8. pipeline_manager->start()
    // 9. g_main_loop_run()
    // 10. Check pipeline state + arm watchdog
    // 11. Cleanup: stop → unref loop → reset managers → gst_deinit → spdlog::shutdown()
}
```

### CMakeLists.txt

```cmake
# app/CMakeLists.txt
add_executable(vms_engine app/main.cpp)

target_link_libraries(vms_engine
    PRIVATE vms_engine_pipeline
    PRIVATE vms_engine_infrastructure
    PRIVATE vms_engine_domain
    PRIVATE vms_engine_services
    PRIVATE PkgConfig::GST
    PRIVATE PkgConfig::GLIB2
    PRIVATE spdlog::spdlog
    PRIVATE fmt::fmt
    PRIVATE Threads::Threads
)
```

---

## Part C: Plugins (Custom Parsers)

Plugins are shared libraries loaded at runtime by DeepStream via `custom-lib-path` in nvinfer config files. They depend only on DeepStream SDK headers and export C functions — they do not use core/domain/infrastructure.

### Files to Create

| File                                                      | Purpose                           |
| --------------------------------------------------------- | --------------------------------- |
| `plugins/src/nvdsinfer_custom_impl_Yolo.cpp`              | YOLO bounding box parser          |
| `plugins/src/nvdsinfer_custom_impl_Yolo_face.cpp`         | YOLO face detection parser        |
| `plugins/src/nvdsinfer_custom_impl_Yolo_padded.cpp`       | YOLO padded output parser         |
| `plugins/src/nvdsinfer_person_attrib_deepmar.cpp`         | Person attribute parser (DeepMAR) |
| `plugins/src/nvdsinfer_person_attrib_deepmar_24label.cpp` | DeepMAR 24-label variant          |
| `plugins/src/ocr_fast_plate_parser.cpp`                   | License plate OCR parser          |
| `plugins/src/ocr_fast_plate_parser_vn.cpp`                | Vietnamese plate OCR parser       |

### CMakeLists.txt

```cmake
# plugins/CMakeLists.txt

set(PLUGIN_SOURCES
    src/nvdsinfer_custom_impl_Yolo.cpp
    src/nvdsinfer_custom_impl_Yolo_face.cpp
    src/nvdsinfer_custom_impl_Yolo_padded.cpp
    src/nvdsinfer_person_attrib_deepmar.cpp
    src/nvdsinfer_person_attrib_deepmar_24label.cpp
    src/ocr_fast_plate_parser.cpp
    src/ocr_fast_plate_parser_vn.cpp
)

foreach(PLUGIN_SRC ${PLUGIN_SOURCES})
    get_filename_component(PLUGIN_NAME ${PLUGIN_SRC} NAME_WE)
    add_library(${PLUGIN_NAME} SHARED ${PLUGIN_SRC})
    target_include_directories(${PLUGIN_NAME}
        PRIVATE ${DEEPSTREAM_DIR}/sources/includes
    )
    target_link_libraries(${PLUGIN_NAME}
        PRIVATE ${NVDS_LIBS}
        PRIVATE CUDA::cudart
    )
endforeach()
```

---

## Verification

```bash
# Inside container: docker compose exec app bash
cd /opt/vms_engine

# 1. Compile services
cmake --build build --target vms_engine_services -- -j5

# 2. Compile main binary
cmake --build build --target vms_engine -- -j5

# 3. Compile all plugins
cmake --build build -- -j5

# 4. Check engine:: namespace in services
grep -r "engine::services" services/ --include="*.hpp" --include="*.cpp" | head -5

# 5. Check no #ifdef backend guards
grep -r "LANTANA_WITH_DEEPSTREAM\|LANTANA_WITH_DLSTREAMER\|WITH_DLSTREAMER" app/ \
    --include="*.cpp" --include="*.hpp" && echo "FAIL" || echo "PASS"

# 6. Verify binary exists
ls -la build/bin/vms_engine

# 7. Verify plugins built
find build/ -name "*.so" | sort
```

---

## Checklist

- [ ] Services: 2 files created in `engine::services` namespace
- [ ] `app/main.cpp`: Written without `#ifdef` guards (~450 lines)
- [ ] `create_pipeline_manager()`: Direct construction (`make_shared<PipelineManager>()`)
- [ ] Logger name: `"vms_engine"`
- [ ] All includes use `"engine/..."` paths
- [ ] Plugins: 7 files created, build each as separate `.so`
- [ ] Plugin CMake loops over source list
- [ ] Final binary `vms_engine` links successfully
- [ ] No `LANTANA_WITH_*` or `backends/` references in source
