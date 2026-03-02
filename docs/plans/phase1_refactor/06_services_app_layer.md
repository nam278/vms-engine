# Plan 06 — Services, Application Entry Point, and Plugins

> Migrate external services (`services/`), rewrite `app/main.cpp`, and copy `plugins/`.
> This is the final code migration plan before integration testing.

---

## Prerequisites

- Plans 02–05 completed (all libraries compile independently)

## Deliverables

- [ ] Triton inference client (1 header + 1 source) migrated
- [ ] `app/main.cpp` rewritten (remove #ifdef guards, use `engine::` namespace)
- [ ] All 7 plugin source files copied
- [ ] Final binary links and compiles

---

## Part A: External Services (Triton Client)

### File Migration Table

| Source (lantanav2)                                           | Target (vms-engine)                                           | Changes   |
| ------------------------------------------------------------ | ------------------------------------------------------------- | ---------- |
| `services/include/lantana/services/triton_inference_client.hpp` | `services/include/engine/services/triton_inference_client.hpp` | Namespace |
| `services/src/triton_inference_client.cpp`                    | `services/src/triton_inference_client.cpp`                     | Namespace + includes |

### Changes

- `namespace lantana::services` → `namespace engine::services`
- `#include "lantana/core/services/..."` → `#include "engine/core/services/..."`
- Implement `engine::core::services::IExternalInferenceClient` (if not already)

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

The original `app/main.cpp` is **546 lines**. Key changes during rewrite:

### What Gets Removed

1. **#ifdef backend guards** — All `LANTANA_WITH_DEEPSTREAM` / `LANTANA_WITH_DLSTREAMER` blocks
2. **Backend selection logic** — `create_pipeline_manager()` no longer switches on `backend_type`
3. **DLStreamer code paths** — All dead code / commented-out DLStreamer references
4. **`std::variant` ParseResult handling** — Simplify to direct config struct

### What Gets Changed

1. **Namespace**: `lantana::` → `engine::` everywhere
2. **Include paths**: All `lantana/` → `engine/`
3. **Logger name**: `"lantana_app"` → `"vms_engine"`
4. **Default config path**: `"config/deepstream_default.yml"` → `"config/default.yml"` (or keep as-is)
5. **Pipeline manager creation**: Direct construction, no factory switch

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

### Detailed Diff Summary

| Section | Lines | Change Type |
| --- | --- | --- |
| Includes (1-19) | 19 | Replace paths, remove `#ifdef` includes |
| Global vars (39-43) | 5 | `lantana::` → `engine::` |
| `handle_signal` (45-69) | 25 | Unchanged logic, namespace update |
| `handle_crash_signal` (71-115) | 45 | Unchanged |
| `parse_arguments` (117-131) | 15 | Optional: update default config path |
| `initialize_logger` (133-155) | 23 | `lantana::core::utils::` → `engine::core::utils::`, name `"vms_engine"` |
| `create_pipeline_manager` (157-205) | 49 → **5** | **Major simplification**: remove backend switch, #ifdef |
| `main()` steps 1-4 (207-300) | 94 | Namespace updates, remove `std::variant` handling |
| `main()` steps 5-9 (301-360) | 60 | Namespace updates |
| `main()` steps 10-11 (360-546) | 186 | Namespace updates |
| **Total** | **546** | **~450 lines after cleanup** |

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

Plugins are shared libraries loaded at runtime by DeepStream via `custom-lib-path` in nvinfer config files. They don't depend on core/domain/infrastructure — they only depend on DeepStream SDK headers.

### File Migration Table

| Source (lantanav2)                                           | Target (vms-engine)                        | Changes         |
| ------------------------------------------------------------ | ------------------------------------------ | ---------------- |
| `plugins/src/nvdsinfer_custom_impl_Yolo.cpp`               | `plugins/src/nvdsinfer_custom_impl_Yolo.cpp` | Include paths if any |
| `plugins/src/nvdsinfer_custom_impl_Yolo_face.cpp`          | `plugins/src/nvdsinfer_custom_impl_Yolo_face.cpp` | Include paths   |
| `plugins/src/nvdsinfer_custom_impl_Yolo_padded.cpp`        | `plugins/src/nvdsinfer_custom_impl_Yolo_padded.cpp` | Include paths   |
| `plugins/src/nvdsinfer_person_attrib_deepmar.cpp`           | `plugins/src/nvdsinfer_person_attrib_deepmar.cpp` | Include paths   |
| `plugins/src/nvdsinfer_person_attrib_deepmar_24label.cpp`   | `plugins/src/nvdsinfer_person_attrib_deepmar_24label.cpp` | Include paths   |
| `plugins/src/ocr_fast_plate_parser.cpp`                     | `plugins/src/ocr_fast_plate_parser.cpp`     | Include paths   |
| `plugins/src/ocr_fast_plate_parser_vn.cpp`                  | `plugins/src/ocr_fast_plate_parser_vn.cpp`  | Include paths   |

These files are typically standalone — they include DeepStream SDK headers directly and export C functions. **Minimal or no changes needed** beyond copying.

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
# 1. Compile services
cmake --build build --target vms_engine_services -- -j$(nproc)

# 2. Compile main binary
cmake --build build --target vms_engine -- -j$(nproc)

# 3. Compile all plugins
cmake --build build -- -j$(nproc)

# 4. Check no lantana references in main binary code
grep -r "lantana" app/ services/ plugins/ && echo "FAIL" || echo "PASS"

# 5. Check no #ifdef backend guards
grep -r "LANTANA_WITH_DEEPSTREAM\|LANTANA_WITH_DLSTREAMER" app/ && echo "FAIL" || echo "PASS"

# 6. Verify binary exists
ls -la build/bin/vms_engine

# 7. Verify plugins built
ls -la build/lib/*.so 2>/dev/null || ls -la build/plugins/*.so 2>/dev/null
```

---

## Checklist

- [ ] Services: 2 files migrated, namespace updated
- [ ] `app/main.cpp`: Rewritten without #ifdef guards (~100 lines removed)
- [ ] `create_pipeline_manager()`: Direct construction (no backend switch)
- [ ] Logger name: `"vms_engine"`
- [ ] All `lantana::` → `engine::` namespace
- [ ] All include paths updated to `engine/`
- [ ] Plugins: 7 files copied (minimal changes)
- [ ] Plugin CMake builds each as separate `.so`
- [ ] Final binary `vms_engine` links and compiles
- [ ] No remaining `lantana`, `LANTANA_WITH_*`, or `backends/` references
