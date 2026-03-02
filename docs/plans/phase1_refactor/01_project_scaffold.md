# Plan 01 — Project Scaffold & Build System

> Create the vms-engine directory tree, root CMakeLists.txt, sub-module CMake files,
> Docker files, and initial configs. **No C++ source code yet** — just the skeleton.

---

## Prerequisites

- Empty vms-engine repo with only `docs/` and empty root files
- DeepStream SDK 7.1 installed at `/opt/nvidia/deepstream/deepstream`
- CMake 3.16+, GCC/G++ with C++17 support

## Deliverables

- [ ] Root `CMakeLists.txt` with all dependencies
- [ ] Subdirectory `CMakeLists.txt` for each layer (7 files)
- [ ] Directory structure with empty placeholder files
- [ ] `Dockerfile` and `Dockerfile.image` updated
- [ ] `docker-compose.yml` updated
- [ ] `.env.example` with documented variables
- [ ] `configs/default.yml` copied from lantanav2 and adjusted
- [ ] `.clang-format` copied
- [ ] `.gitignore` updated
- [ ] Build succeeds (empty libraries, no source)

---

## Tasks

### 1.1 Create Directory Structure

```bash
mkdir -p core/include/engine/core/{builders,config,pipeline,eventing,probes,handlers,messaging,storage,recording,runtime,services,utils}
mkdir -p core/src/{utils,handlers}

mkdir -p pipeline/include/engine/pipeline/{block_builders,builders,linking,probes,event_handlers,config,services}
mkdir -p pipeline/src/{block_builders,builders,linking,probes,event_handlers,config,services}

mkdir -p domain/include/engine/domain
mkdir -p domain/src

mkdir -p infrastructure/config_parser/include/engine/infrastructure/config_parser
mkdir -p infrastructure/config_parser/src
mkdir -p infrastructure/messaging/include/engine/infrastructure/messaging
mkdir -p infrastructure/messaging/src
mkdir -p infrastructure/storage/include/engine/infrastructure/storage
mkdir -p infrastructure/storage/src
mkdir -p infrastructure/rest_api/include/engine/infrastructure/rest_api
mkdir -p infrastructure/rest_api/src

mkdir -p services/include/engine/services
mkdir -p services/src

mkdir -p plugins/src

mkdir -p app

mkdir -p configs/nvinfer configs/tracker configs/analytics
mkdir -p models
mkdir -p scripts
```

### 1.2 Root CMakeLists.txt

Create new `CMakeLists.txt` based on lantanav2 with these changes:

| Change                              | Details                                                |
| ----------------------------------- | ------------------------------------------------------ |
| Project name                        | `project(vms_engine VERSION 1.0.0 LANGUAGES CXX)`     |
| Remove `LANTANA_WITH_DLSTREAMER`    | Delete option and all DLStreamer pkg_check_modules      |
| Remove `LANTANA_WITH_DEEPSTREAM`    | Always on — use direct `find_library` calls            |
| Remove `add_definitions(-DLANTANA_WITH_DEEPSTREAM)` | Not needed anymore               |
| Backend vars                        | Remove `LANTANA_BACKEND_LIBS`, `LANTANA_BACKEND_SDK_LIBS`, etc. |
| Subdirectories                      | `add_subdirectory(pipeline)` instead of `add_subdirectory(backends)` |
| Executable name                     | `vms_engine` (in app/CMakeLists.txt)                   |
| Library prefix                      | `vms_engine_core`, `vms_engine_pipeline`, etc.         |
| Format target                       | Update glob paths (`backends/` → `pipeline/`)          |

**Key structure:**

```cmake
cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(vms_engine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# --- External Dependencies ---
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB2 REQUIRED IMPORTED_TARGET glib-2.0>=2.56 gobject-2.0>=2.56 gio-2.0>=2.56)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET
    gstreamer-1.0>=1.14 gstreamer-base-1.0>=1.14
    gstreamer-video-1.0>=1.14 gstreamer-app-1.0>=1.14 gstreamer-rtsp-1.0>=1.14)
pkg_check_modules(CURL REQUIRED libcurl)
find_package(Threads REQUIRED)
find_package(CUDA)
find_package(CUDAToolkit REQUIRED)

# --- DeepStream SDK ---
set(DEEPSTREAM_DIR "/opt/nvidia/deepstream/deepstream" CACHE PATH "DeepStream installation path")
# ... find_library calls for nvds_meta, nvdsgst_meta, etc. ...

# --- FetchContent ---
include(FetchContent)
FetchContent_Declare(spdlog GIT_REPOSITORY https://github.com/gabime/spdlog.git GIT_TAG v1.12.0)
FetchContent_Declare(yaml-cpp GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git GIT_TAG master)
FetchContent_Declare(hiredis GIT_REPOSITORY https://github.com/redis/hiredis.git GIT_TAG v1.3.0)
FetchContent_Declare(nlohmann_json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.12.0)
FetchContent_MakeAvailable(spdlog yaml-cpp hiredis nlohmann_json)

# --- Subdirectories ---
add_subdirectory(core)
add_subdirectory(pipeline)
add_subdirectory(domain)
add_subdirectory(infrastructure)
add_subdirectory(services)
add_subdirectory(plugins)
add_subdirectory(app)
```

### 1.3 Sub-Module CMakeLists.txt Files

#### core/CMakeLists.txt

```cmake
add_library(vms_engine_core STATIC
    src/utils/spdlog_logger.cpp
    src/utils/uuid_v7_generator.cpp
    # src/handlers/handler_registry.cpp  # Added in Plan 02
)

target_include_directories(vms_engine_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PUBLIC ${DEEPSTREAM_DIR}/sources/includes    # For GStreamer/NvDs forward declarations
)

target_link_libraries(vms_engine_core
    PUBLIC spdlog::spdlog
    PUBLIC PkgConfig::GLIB2
    PUBLIC PkgConfig::GST
)
```

#### pipeline/CMakeLists.txt

```cmake
add_library(vms_engine_pipeline STATIC
    # Sources added incrementally in Plan 03
)

target_include_directories(vms_engine_pipeline
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PUBLIC ${DEEPSTREAM_DIR}/sources/includes
    PRIVATE ${CUDA_INCLUDE_DIRS}
)

target_link_libraries(vms_engine_pipeline
    PUBLIC vms_engine_core
    PRIVATE ${DEEPSTREAM_SDK_LIBS}
    PRIVATE PkgConfig::CURL
    PRIVATE CUDA::cudart
)
```

#### domain/CMakeLists.txt

```cmake
add_library(vms_engine_domain STATIC
    # Sources added in Plan 04
)

target_include_directories(vms_engine_domain
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(vms_engine_domain
    PUBLIC vms_engine_core
)
```

#### infrastructure/CMakeLists.txt

```cmake
add_library(vms_engine_infrastructure STATIC
    # Sources added in Plan 05
)

target_include_directories(vms_engine_infrastructure
    PUBLIC config_parser/include
    PUBLIC messaging/include
    PUBLIC storage/include
    PUBLIC rest_api/include
)

target_link_libraries(vms_engine_infrastructure
    PUBLIC vms_engine_core
    PRIVATE yaml-cpp::yaml-cpp
    PRIVATE hiredis::hiredis_static
    PRIVATE PkgConfig::CURL
)
```

#### services/CMakeLists.txt

```cmake
add_library(vms_engine_services STATIC
    # Sources added in Plan 06
)

target_include_directories(vms_engine_services
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(vms_engine_services
    PUBLIC vms_engine_core
    PRIVATE PkgConfig::CURL
)
```

#### app/CMakeLists.txt

```cmake
add_executable(vms_engine main.cpp)

target_link_libraries(vms_engine
    PRIVATE vms_engine_core
    PRIVATE vms_engine_pipeline
    PRIVATE vms_engine_domain
    PRIVATE vms_engine_infrastructure
    PRIVATE vms_engine_services
    PRIVATE Threads::Threads
)
```

#### plugins/CMakeLists.txt

```cmake
# Each plugin is a shared library loaded by DeepStream at runtime
# Copied from lantanav2 with minimal changes
```

### 1.4 Copy Build Support Files

| File               | Source (lantanav2)     | Action                                     |
| ------------------ | ---------------------- | ------------------------------------------ |
| `.clang-format`    | `.clang-format`        | Copy as-is                                 |
| `.gitignore`       | `.gitignore`           | Copy, add `build/`, `lantana_data/`        |
| `Dockerfile`       | `Dockerfile`           | Update binary name `lantana` → `vms_engine`|
| `Dockerfile.image` | `Dockerfile.image`     | Copy, update project name in comments      |
| `docker-compose.yml`| `docker-compose.yml`  | Update container/image names               |
| `.env.example`     | `.env.example`         | Copy, document all env vars                |

### 1.5 Copy and Adjust Config Files

```bash
cp lantanav2/configs/deepstream_default.yml vms-engine/configs/default.yml
cp -r lantanav2/configs/nvinfer/ vms-engine/configs/nvinfer/
cp -r lantanav2/configs/tracker/ vms-engine/configs/tracker/
```

In `configs/default.yml`:
- Change `application.name` to `"vms_engine"`
- No other structural changes needed (YAML is backend-agnostic)

### 1.6 Create Minimal main.cpp Stub

```cpp
// app/main.cpp
#include <iostream>

int main(int argc, char* argv[]) {
    std::cout << "vms_engine v1.0.0 — scaffold build OK" << std::endl;
    return 0;
}
```

This ensures `cmake --build build` succeeds even with empty libraries.

---

## Verification

```bash
# 1. Configure
cmake -S . -B build -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream

# 2. Build
cmake --build build -- -j$(nproc)

# 3. Run
./build/bin/vms_engine
# Expected output: "vms_engine v1.0.0 — scaffold build OK"

# 4. Check no lantana references
grep -r "lantana" CMakeLists.txt app/ core/ pipeline/ domain/ infrastructure/ services/ plugins/ || echo "PASS: No lantana references"
```

---

## Checklist

- [ ] Directory tree created per architecture blueprint
- [ ] Root CMakeLists.txt with all external deps (no DLStreamer)
- [ ] 7 sub-module CMakeLists.txt files
- [ ] Stub main.cpp compiles and runs
- [ ] Docker files updated
- [ ] Config files copied and adjusted
- [ ] `.clang-format` and `.gitignore` in place
- [ ] `cmake configure` succeeds
- [ ] `cmake build` succeeds
- [ ] No `lantana` string in any project file (except docs/)
