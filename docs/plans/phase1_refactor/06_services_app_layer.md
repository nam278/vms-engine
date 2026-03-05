---
goal: "Plan 06 — Application Entry Point and Plugins"
version: "1.0"
date_created: "2025-01-15"
last_updated: "2025-07-17"
owner: "VMS Engine Team"
status: "Planned"
tags: [application, main, plugins, deepstream, entry-point, c++17]
---

# Plan 06 — Application Entry Point and Plugins

![Status: Planned](https://img.shields.io/badge/status-Planned-blue)

Write `app/main.cpp` and create `plugins/`.
This is the final implementation plan before integration testing.

---

## 1. Requirements & Constraints

- **REQ-001**: Plans 02–05 completed — all libraries compile independently.
- **REQ-002**: `app/main.cpp` is DeepStream-only — no `#ifdef` guards, no backend variants.
- **REQ-003**: All 7 plugin shared libraries build and export C functions for DeepStream custom parsing.
- **REQ-004**: Final binary `vms_engine` links and compiles against all layers.
- **CON-001**: No `LANTANA_WITH_DEEPSTREAM` or `LANTANA_WITH_DLSTREAMER` guards.
- **CON-002**: All includes use `"engine/..."` paths — no `"lantana/..."` references.
- **CON-003**: Plugins depend only on DeepStream SDK headers — they do NOT use core/domain/infrastructure.
- **GUD-001**: `create_pipeline_manager()` calls `std::make_shared<engine::pipeline::PipelineManager>()` directly — no factory dispatch.
- **GUD-002**: Parser returns `PipelineConfig` directly — no `std::variant` wrapping.
- **PAT-001**: Direct construction pattern — `main.cpp` wires all layers together at startup.

---

## 2. Implementation Steps

### Part A — Application Entry Point (main.cpp)

`app/main.cpp` is the single entry point (~450 lines). DeepStream-only — no backend switching.

**GOAL-001**: Write `app/main.cpp` with direct construction pattern.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-001 | Create `app/main.cpp` with signal handlers (`handle_signal`, `handle_crash_signal`) | ☐ | |
| TASK-002 | Implement `parse_arguments(argc, argv)` — `-c configs/default.yml` default | ☐ | |
| TASK-003 | Implement `initialize_logger()` — logger name `"vms_engine"`, uses PipelineConfig log settings | ☐ | |
| TASK-004 | Implement `create_pipeline_manager()` — `std::make_shared<engine::pipeline::PipelineManager>()` | ☐ | |
| TASK-005 | Implement 11-step `main()` flow: parse → parse config → init logger → log summary → create loop → create manager → register signals → start → run loop → check state → cleanup | ☐ | |
| TASK-006 | Create `app/CMakeLists.txt` linking all layer targets | ☐ | |

#### Design Decisions

1. **DeepStream-only** — No `#ifdef LANTANA_WITH_DEEPSTREAM` guards; DeepStream is the only pipeline backend.
2. **Direct construction** — `create_pipeline_manager()` calls `std::make_shared<engine::pipeline::PipelineManager>()`.
3. **Flat config** — Parser returns `PipelineConfig` directly; no `std::variant`.
4. **Logger name**: `"vms_engine"`.
5. **Default config path**: `-c configs/default.yml`.

#### Rewritten Structure (Pseudocode)

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

namespace {

GMainLoop* g_main_loop = nullptr;
std::shared_ptr<engine::core::pipeline::IPipelineManager> g_pipeline_manager = nullptr;
std::atomic<bool> g_stop_called{false};

void handle_signal(int signum) { /* graceful stop via g_main_loop_quit */ }
void handle_crash_signal(int signum) { /* stack trace + abort */ }

std::string parse_arguments(int argc, char* argv[]) {
    // -c <config_path>, default: "configs/default.yml"
}

bool initialize_logger(const engine::core::config::PipelineConfig* config,
                       const std::string& error_msg) {
    // Logger name: "vms_engine"
    // Use config->pipeline.log_level if available
}

// Direct construction — no backend_type switch, no #ifdef
std::shared_ptr<engine::core::pipeline::IPipelineManager>
create_pipeline_manager() {
    return std::make_shared<engine::pipeline::PipelineManager>();
}

}  // anonymous namespace

int main(int argc, char* argv[]) {
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

#### app/CMakeLists.txt

```cmake
# app/CMakeLists.txt
add_executable(vms_engine app/main.cpp)

target_link_libraries(vms_engine
    PRIVATE vms_engine_pipeline
    PRIVATE vms_engine_infrastructure
    PRIVATE vms_engine_domain
    PRIVATE PkgConfig::GST
    PRIVATE PkgConfig::GLIB2
    PRIVATE spdlog::spdlog
    PRIVATE fmt::fmt
    PRIVATE Threads::Threads
)
```

---

### Part B — Plugins (Custom Parsers)

Plugins are shared libraries loaded at runtime by DeepStream via `custom-lib-path` in nvinfer config files.
They depend only on DeepStream SDK headers and export C functions — they do not use core/domain/infrastructure.

**GOAL-002**: Create 7 plugin source files and build each as `.so`.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-007 | Create `nvdsinfer_custom_impl_Yolo.cpp` — YOLO bounding box parser | ☐ | |
| TASK-008 | Create `nvdsinfer_custom_impl_Yolo_face.cpp` — YOLO face detection parser | ☐ | |
| TASK-009 | Create `nvdsinfer_custom_impl_Yolo_padded.cpp` — YOLO padded output parser | ☐ | |
| TASK-010 | Create `nvdsinfer_person_attrib_deepmar.cpp` — Person attribute parser (DeepMAR) | ☐ | |
| TASK-011 | Create `nvdsinfer_person_attrib_deepmar_24label.cpp` — DeepMAR 24-label variant | ☐ | |
| TASK-012 | Create `ocr_fast_plate_parser.cpp` — License plate OCR parser | ☐ | |
| TASK-013 | Create `ocr_fast_plate_parser_vn.cpp` — Vietnamese plate OCR parser | ☐ | |
| TASK-014 | Create `plugins/CMakeLists.txt` with foreach loop over plugin sources | ☐ | |

#### Plugin Files

| File | Purpose |
|------|---------|
| `plugins/src/nvdsinfer_custom_impl_Yolo.cpp` | YOLO bounding box parser |
| `plugins/src/nvdsinfer_custom_impl_Yolo_face.cpp` | YOLO face detection parser |
| `plugins/src/nvdsinfer_custom_impl_Yolo_padded.cpp` | YOLO padded output parser |
| `plugins/src/nvdsinfer_person_attrib_deepmar.cpp` | Person attribute parser (DeepMAR) |
| `plugins/src/nvdsinfer_person_attrib_deepmar_24label.cpp` | DeepMAR 24-label variant |
| `plugins/src/ocr_fast_plate_parser.cpp` | License plate OCR parser |
| `plugins/src/ocr_fast_plate_parser_vn.cpp` | Vietnamese plate OCR parser |

#### plugins/CMakeLists.txt

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

## 3. Alternatives

- **ALT-001**: Factory pattern for `create_pipeline_manager()` dispatching on backend type (rejected — single DeepStream backend, no dispatch needed).
- **ALT-002**: Plugins as static libraries linked into main binary (rejected — DeepStream requires `.so` loaded via `custom-lib-path` at runtime).
- **ALT-003**: Plugin sources under `pipeline/` directory (rejected — plugins are DeepStream-SDK-only, no architecture layer dependency).

---

## 4. Dependencies

- **DEP-001**: Plans 02–05 completed — all layer libraries compile.
- **DEP-002**: DeepStream SDK 8.0 — plugin headers from `${DEEPSTREAM_DIR}/sources/includes`.
- **DEP-003**: CUDA runtime — plugin linking.

---

## 5. Files

| ID | File Path | Description |
|----|-----------|-------------|
| FILE-001 | `app/main.cpp` | Application entry point (~450 lines) |
| FILE-002 | `app/CMakeLists.txt` | Links all layer targets into `vms_engine` binary |
| FILE-003 | `plugins/src/nvdsinfer_custom_impl_Yolo.cpp` | YOLO bbox parser |
| FILE-004 | `plugins/src/nvdsinfer_custom_impl_Yolo_face.cpp` | YOLO face parser |
| FILE-005 | `plugins/src/nvdsinfer_custom_impl_Yolo_padded.cpp` | YOLO padded parser |
| FILE-006 | `plugins/src/nvdsinfer_person_attrib_deepmar.cpp` | DeepMAR person attribute |
| FILE-007 | `plugins/src/nvdsinfer_person_attrib_deepmar_24label.cpp` | DeepMAR 24-label variant |
| FILE-008 | `plugins/src/ocr_fast_plate_parser.cpp` | License plate OCR |
| FILE-009 | `plugins/src/ocr_fast_plate_parser_vn.cpp` | Vietnamese plate OCR |
| FILE-010 | `plugins/CMakeLists.txt` | foreach loop building each plugin as `.so` |

---

## 6. Testing & Verification

- **TEST-001**: Compile main binary — `cmake --build build --target vms_engine -- -j5`.
- **TEST-002**: Compile all plugins — `cmake --build build -- -j5`.
- **TEST-003**: No `#ifdef` backend guards — `grep -r "LANTANA_WITH_DEEPSTREAM\|LANTANA_WITH_DLSTREAMER" app/ --include="*.cpp" --include="*.hpp" && echo "FAIL" || echo "PASS"`.
- **TEST-004**: Verify binary exists — `ls -la build/bin/vms_engine`.
- **TEST-005**: Verify plugins built — `find build/ -name "*.so" | sort` (expect 7 .so files).

---

## 7. Risks & Assumptions

- **RISK-001**: Plugin API changes between DeepStream versions; mitigated by targeting DS 8.0 explicitly.
- **ASSUMPTION-001**: `app/main.cpp` is the only entry point — no other binaries.
- **ASSUMPTION-002**: Plugins export standard DeepStream C functions (`NvDsInferParseCustom*`).
- **ASSUMPTION-003**: Cleanup in `main()` follows reverse-construction order: stop pipeline → unref loop → reset managers → gst_deinit → spdlog::shutdown.

---

## 8. Related Specifications

- [Plan 02 — Core Layer](02_core_layer.md) (interfaces consumed by main.cpp)
- [Plan 03 — Pipeline Layer](03_pipeline_layer.md) (PipelineManager constructed here)
- [Plan 05 — Infrastructure Layer](05_infrastructure_layer.md) (adapters wired in main.cpp)
- [Plan 07 — Integration Testing](07_integration_testing.md) (validates binary and plugins)
- [AGENTS.md](../../AGENTS.md)
