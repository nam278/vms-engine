---
goal: "Plan 01 — Project scaffold, directory tree, CMake build system, Docker setup"
version: "1.0"
date_created: "2025-01-15"
last_updated: "2025-07-17"
owner: "VMS Engine Team"
status: "Planned"
tags: [cmake, scaffold, build-system, docker, c++17]
---

# Plan 01 — Project Scaffold & Build System

![Status: Planned](https://img.shields.io/badge/status-Planned-blue)

Create the vms-engine directory tree, root CMakeLists.txt, sub-module CMake files, and initial placeholder files. **No C++ source code yet** — just the skeleton that compiles.

## 1. Requirements & Constraints

- **REQ-001**: Root `CMakeLists.txt` with all dependencies (FetchContent + system)
- **REQ-002**: Subdirectory `CMakeLists.txt` for each layer (6 files)
- **REQ-003**: Directory structure matching Clean Architecture layers
- **REQ-004**: Build succeeds inside container (empty libraries + stub main.cpp)
- **CON-001**: Docker image `vms-engine-dev:latest` built from `Dockerfile` (DeepStream 8.0 base)
- **CON-002**: Container running via `docker compose up -d`, source mounted at `/opt/vms_engine`
- **CON-003**: All work done inside container: `docker compose exec app bash`
- **GUD-001**: `.clang-format` configured for consistent code style
- **GUD-002**: `configs/default.yml` symlinked to canonical config

## 2. Implementation Steps

### Phase 1: Directory Structure

- GOAL-001: Create the complete directory tree for all 5 layers

| Task     | Description                                    | Completed | Date |
| -------- | ---------------------------------------------- | --------- | ---- |
| TASK-001 | Create core layer directories                  |           |      |
| TASK-002 | Create pipeline layer directories              |           |      |
| TASK-003 | Create domain layer directories                |           |      |
| TASK-004 | Create infrastructure layer directories        |           |      |
| TASK-005 | Create app and runtime data directories        |           |      |

```bash
cd /opt/vms_engine

# Core Layer
mkdir -p core/include/engine/core/{builders,config,pipeline,eventing,handlers,messaging,storage,recording,runtime,utils}
mkdir -p core/src/utils

# Pipeline Layer
mkdir -p pipeline/include/engine/pipeline/{block_builders,builders,linking,probes,event_handlers}
mkdir -p pipeline/src/{block_builders,builders,linking,probes,event_handlers}

# Domain Layer
mkdir -p domain/include/engine/domain
mkdir -p domain/src

# Infrastructure Layer
mkdir -p infrastructure/config_parser/include/engine/infrastructure/config_parser
mkdir -p infrastructure/config_parser/src
mkdir -p infrastructure/messaging/include/engine/infrastructure/messaging
mkdir -p infrastructure/messaging/src
mkdir -p infrastructure/storage/include/engine/infrastructure/storage
mkdir -p infrastructure/storage/src

# Application + runtime
mkdir -p app
mkdir -p configs/components
mkdir -p models
mkdir -p scripts
```

### Phase 2: Root CMakeLists.txt

- GOAL-002: Create top-level CMake build configuration with all dependencies

| Task     | Description                                           | Completed | Date |
| -------- | ----------------------------------------------------- | --------- | ---- |
| TASK-006 | Define C++17 standard and output directories          |           |      |
| TASK-007 | Configure system dependencies (PkgConfig, CUDA, GST)  |           |      |
| TASK-008 | Configure DeepStream SDK 8.0 discovery                |           |      |
| TASK-009 | Configure FetchContent (spdlog, yaml-cpp, hiredis, json) |           |      |
| TASK-010 | Add subdirectory targets and clang-format target      |           |      |

```cmake
cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(vms_engine VERSION 1.0.0 LANGUAGES CXX)

# ── C++ Standard ────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Output directories ──────────────────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ── System Dependencies ─────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(CUDAToolkit REQUIRED)

pkg_check_modules(GLIB2 REQUIRED IMPORTED_TARGET
    glib-2.0>=2.56  gobject-2.0>=2.56  gio-2.0>=2.56)

pkg_check_modules(GST REQUIRED IMPORTED_TARGET
    gstreamer-1.0>=1.14
    gstreamer-base-1.0>=1.14
    gstreamer-video-1.0>=1.14
    gstreamer-app-1.0>=1.14
    gstreamer-rtsp-1.0>=1.14)

pkg_check_modules(CURL REQUIRED libcurl)

# ── DeepStream SDK 8.0 ──────────────────────────────────────────────
set(DEEPSTREAM_DIR "/opt/nvidia/deepstream/deepstream"
    CACHE PATH "DeepStream SDK installation path")

set(DEEPSTREAM_INCLUDE_DIRS
    ${DEEPSTREAM_DIR}/sources/includes
    ${DEEPSTREAM_DIR}/sources/includes/nvdsinfer
)

set(DEEPSTREAM_SDK_LIBS "")
foreach(_lib nvds_meta nvdsgst_meta nvds_utils nvdsgst_helper
             nvbufsurface nvbufsurftransform nvds_infer nvds_batch_utils)
    find_library(LIB_${_lib} NAMES ${_lib}
        PATHS ${DEEPSTREAM_DIR}/lib NO_DEFAULT_PATH)
    if(LIB_${_lib})
        list(APPEND DEEPSTREAM_SDK_LIBS ${LIB_${_lib}})
    else()
        message(WARNING "DeepStream library not found: ${_lib}")
    endif()
endforeach()

# ── FetchContent (pinned versions) ──────────────────────────────────
include(FetchContent)

FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1)

FetchContent_Declare(yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        0.8.0)

FetchContent_Declare(hiredis
    GIT_REPOSITORY https://github.com/redis/hiredis.git
    GIT_TAG        v1.3.0)
set(DISABLE_TESTS ON CACHE BOOL "" FORCE)

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3)

FetchContent_MakeAvailable(spdlog yaml-cpp hiredis nlohmann_json)

# ── Sub-modules ─────────────────────────────────────────────────────
add_subdirectory(core)
add_subdirectory(pipeline)
add_subdirectory(domain)
add_subdirectory(infrastructure)
add_subdirectory(app)

# ── clang-format target (optional) ──────────────────────────────────
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    file(GLOB_RECURSE ALL_SOURCES
        core/include/*.hpp core/src/*.cpp
        pipeline/include/*.hpp pipeline/src/*.cpp
        domain/include/*.hpp domain/src/*.cpp
        infrastructure/*/include/*.hpp infrastructure/*/src/*.cpp
        app/*.cpp)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i ${ALL_SOURCES}
        COMMENT "Running clang-format"
        VERBATIM)
endif()
```

### Phase 3: Sub-Module CMakeLists.txt

- GOAL-003: Create per-layer CMake files with correct dependency chains

| Task     | Description                       | Completed | Date |
| -------- | --------------------------------- | --------- | ---- |
| TASK-011 | Create core/CMakeLists.txt        |           |      |
| TASK-012 | Create pipeline/CMakeLists.txt    |           |      |
| TASK-013 | Create domain/CMakeLists.txt      |           |      |
| TASK-014 | Create infrastructure/CMakeLists.txt |           |      |
| TASK-015 | Create app/CMakeLists.txt         |           |      |

#### core/CMakeLists.txt

```cmake
add_library(vms_engine_core STATIC
    src/utils/placeholder.cpp
)

target_include_directories(vms_engine_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PUBLIC ${DEEPSTREAM_INCLUDE_DIRS}
)

target_link_libraries(vms_engine_core
    PUBLIC spdlog::spdlog
    PUBLIC PkgConfig::GLIB2
    PUBLIC PkgConfig::GST
)

set_target_properties(vms_engine_core PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)
```

#### pipeline/CMakeLists.txt

```cmake
add_library(vms_engine_pipeline STATIC
    src/placeholder.cpp
)

target_include_directories(vms_engine_pipeline
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PUBLIC ${DEEPSTREAM_INCLUDE_DIRS}
    PRIVATE ${CUDAToolkit_INCLUDE_DIRS}
)

target_link_libraries(vms_engine_pipeline
    PUBLIC  vms_engine_core
    PRIVATE ${DEEPSTREAM_SDK_LIBS}
    PRIVATE PkgConfig::CURL
    PRIVATE CUDA::cudart
)

set_target_properties(vms_engine_pipeline PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)
```

#### domain/CMakeLists.txt

```cmake
add_library(vms_engine_domain STATIC
    src/placeholder.cpp
)

target_include_directories(vms_engine_domain
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(vms_engine_domain
    PUBLIC vms_engine_core
)

set_target_properties(vms_engine_domain PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)
```

#### infrastructure/CMakeLists.txt

```cmake
add_library(vms_engine_infrastructure STATIC
    config_parser/src/placeholder.cpp
    messaging/src/placeholder.cpp
    storage/src/placeholder.cpp
)

target_include_directories(vms_engine_infrastructure
    PUBLIC config_parser/include
    PUBLIC messaging/include
    PUBLIC storage/include
)

target_link_libraries(vms_engine_infrastructure
    PUBLIC  vms_engine_core
    PRIVATE yaml-cpp::yaml-cpp
    PRIVATE hiredis::hiredis_static
    PRIVATE PkgConfig::CURL
)

set_target_properties(vms_engine_infrastructure PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)
```

#### app/CMakeLists.txt

```cmake
add_executable(vms_engine main.cpp)

target_link_libraries(vms_engine
    PRIVATE vms_engine_core
    PRIVATE vms_engine_pipeline
    PRIVATE vms_engine_domain
    PRIVATE vms_engine_infrastructure
    PRIVATE Threads::Threads
)

add_custom_command(TARGET vms_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_SOURCE_DIR}/configs
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/configs
    COMMENT "Linking configs/ → build/bin/configs/")
```

### Phase 4: Placeholder Sources

- GOAL-004: Create minimal placeholder files so each CMake target compiles

| Task     | Description                          | Completed | Date |
| -------- | ------------------------------------ | --------- | ---- |
| TASK-016 | Create placeholder .cpp for each layer |           |      |
| TASK-017 | Verify full build succeeds           |           |      |

```bash
echo '// placeholder — replaced in Plan 02' > core/src/utils/placeholder.cpp
echo '// placeholder — replaced in Plan 03' > pipeline/src/placeholder.cpp
echo '// placeholder — replaced in Plan 04' > domain/src/placeholder.cpp
echo '// placeholder — replaced in Plan 05' > infrastructure/config_parser/src/placeholder.cpp
echo '// placeholder — replaced in Plan 05' > infrastructure/messaging/src/placeholder.cpp
echo '// placeholder — replaced in Plan 05' > infrastructure/storage/src/placeholder.cpp
```

## 3. Alternatives

- **ALT-001**: Single CMakeLists.txt for everything (rejected — per-layer CMake enables independent compilation and clearer dependency enforcement)
- **ALT-002**: Meson or Bazel instead of CMake (rejected — CMake is standard for DeepStream projects and familiar to team)
- **ALT-003**: vcpkg/Conan for dependency management (rejected — FetchContent is simpler for pinned versions in container environment)

## 4. Dependencies

- **DEP-001**: Docker image `vms-engine-dev:latest` built from `Dockerfile`
- **DEP-002**: DeepStream 8.0 container base (`nvcr.io/nvidia/deepstream:8.0-gc-triton-devel`)
- **DEP-003**: No prior plan dependencies — this is the first plan

## 5. Files

- **FILE-001**: `CMakeLists.txt` — Root CMake configuration
- **FILE-002**: `core/CMakeLists.txt` — Core layer target
- **FILE-003**: `pipeline/CMakeLists.txt` — Pipeline layer target
- **FILE-004**: `domain/CMakeLists.txt` — Domain layer target
- **FILE-005**: `infrastructure/CMakeLists.txt` — Infrastructure layer target
- **FILE-006**: `app/CMakeLists.txt` — Application executable target
- **FILE-007**: `core/src/utils/placeholder.cpp` — Placeholder source
- **FILE-008**: `pipeline/src/placeholder.cpp` — Placeholder source
- **FILE-009**: `domain/src/placeholder.cpp` — Placeholder source
- **FILE-010**: `infrastructure/config_parser/src/placeholder.cpp` — Placeholder source
- **FILE-011**: `infrastructure/messaging/src/placeholder.cpp` — Placeholder source
- **FILE-012**: `infrastructure/storage/src/placeholder.cpp` — Placeholder source
- **FILE-013**: `configs/default.yml` — Default pipeline config (symlink)
- **FILE-014**: `.clang-format` — Code formatting rules

## 6. Testing

- **TEST-001**: Verify CMake configure succeeds — `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -G Ninja`
- **TEST-002**: Verify full build succeeds — `cmake --build build -- -j5`
- **TEST-003**: Verify binary exists — `ls build/bin/vms_engine`
- **TEST-004**: Verify configs symlink — `ls build/bin/configs/`
- **TEST-005**: Verify clang-format target — `cmake --build build --target format`

## 7. Risks & Assumptions

- **RISK-001**: FetchContent download fails in container — **Mitigation**: Pre-cache dependencies or use local mirrors
- **RISK-002**: DeepStream SDK path mismatch — **Mitigation**: Validate `DEEPSTREAM_DIR` in CMake with explicit warnings
- **ASSUMPTION-001**: Docker container has internet access for FetchContent downloads
- **ASSUMPTION-002**: DeepStream SDK is installed at `/opt/nvidia/deepstream/deepstream`
- **ASSUMPTION-003**: Ninja generator is available in the container

## 8. Related Specifications / Further Reading

- [00_overview.md](00_overview.md) — Phase 1 Master Plan
- [docs/architecture/CMAKE.md](../../architecture/CMAKE.md) — Build system reference
- [AGENTS.md](../../../AGENTS.md) — Project overview and conventions
- [Dockerfile](../../../Dockerfile) — Dev container definition
