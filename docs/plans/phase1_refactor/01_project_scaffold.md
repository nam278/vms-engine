# Plan 01 — Project Scaffold & Build System

> Create the vms-engine directory tree, root CMakeLists.txt, sub-module CMake files,
> and initial placeholder files. **No C++ source code yet** — just the skeleton that compiles.

---

## Prerequisites

- Docker image `vms-engine-dev:latest` built from `Dockerfile` (DeepStream 8.0 base)
- Container running via `docker compose up -d`, source mounted at `/opt/vms_engine`
- All work done inside container: `docker compose exec app bash`

## Deliverables

- [ ] Root `CMakeLists.txt` with all dependencies (FetchContent + system)
- [ ] Subdirectory `CMakeLists.txt` for each layer (6 files)
- [ ] Directory structure with placeholder `.gitkeep` files
- [ ] `configs/default.yml` → symlink or copy of `docs/configs/deepstream_default.yml`
- [ ] `.clang-format` configured
- [ ] Build succeeds inside container (empty libraries + stub main.cpp)

---

## Tasks

### 1.1 Create Directory Structure

```bash
# Run inside container: docker compose exec app bash
cd /opt/vms_engine

# ── Core Layer ──────────────────────────────────────────────────────
mkdir -p core/include/engine/core/{builders,config,pipeline,eventing,handlers,messaging,storage,recording,runtime,utils}
mkdir -p core/src/utils

# ── Pipeline Layer (DeepStream element builders + linking) ──────────
mkdir -p pipeline/include/engine/pipeline/{block_builders,builders,linking,probes,event_handlers}
mkdir -p pipeline/src/{block_builders,builders,linking,probes,event_handlers}

# ── Domain Layer (business logic — thin for now) ────────────────────
mkdir -p domain/include/engine/domain
mkdir -p domain/src

# ── Infrastructure Layer (config parser, messaging, storage) ────────
mkdir -p infrastructure/config_parser/include/engine/infrastructure/config_parser
mkdir -p infrastructure/config_parser/src
mkdir -p infrastructure/messaging/include/engine/infrastructure/messaging
mkdir -p infrastructure/messaging/src
mkdir -p infrastructure/storage/include/engine/infrastructure/storage
mkdir -p infrastructure/storage/src

# ── Application entry point ─────────────────────────────────────────
mkdir -p app

# ── Runtime data directories ────────────────────────────────────────
mkdir -p configs/components
mkdir -p models
mkdir -p scripts
```

### 1.2 Root CMakeLists.txt

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

# DeepStream libraries
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

### 1.3 Sub-Module CMakeLists.txt Files

#### core/CMakeLists.txt

```cmake
add_library(vms_engine_core STATIC
    src/utils/placeholder.cpp   # Will be replaced in Plan 02
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

# Copy config to build output
add_custom_command(TARGET vms_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_SOURCE_DIR}/configs
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/configs
    COMMENT "Linking configs/ → build/bin/configs/")
```

### 1.4 Create Placeholder Source Files

Each CMake STATIC library needs at least one source file. Create minimal placeholders:

```bash
# core
echo '// placeholder — replaced in Plan 02' > core/src/utils/placeholder.cpp

# pipeline
echo '// placeholder — replaced in Plan 03' > pipeline/src/placeholder.cpp

# domain
echo '// placeholder — replaced in Plan 04' > domain/src/placeholder.cpp

# infrastructure
echo '// placeholder — replaced in Plan 05' > infrastructure/config_parser/src/placeholder.cpp
echo '// placeholder — replaced in Plan 05' > infrastructure/messaging/src/placeholder.cpp
echo '// placeholder — replaced in Plan 05' > infrastructure/storage/src/placeholder.cpp
```

### 1.5 Create Minimal main.cpp Stub

```cpp
// app/main.cpp
#include <iostream>

int main(int argc, char* argv[]) {
    std::cout << "vms_engine v1.0.0 — scaffold build OK" << std::endl;
    return 0;
}
```

### 1.6 Config Files

The canonical config already exists at `docs/configs/deepstream_default.yml`.
Create a working default config:

```bash
cp docs/configs/deepstream_default.yml configs/default.yml
```

### 1.7 Copy .clang-format

Copy from lantanav2 (or create a standard one):

```bash
# From lantanav2 if available:
cp /path/to/lantanav2/.clang-format .clang-format
```

---

## Build & Verification (Inside Container)

```bash
# 1. Start container (from host)
docker compose up -d

# 2. Attach shell
docker compose exec app bash

# 3. Configure (inside container, /opt/vms_engine)
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream \
    -G Ninja

# 4. Build
cmake --build build -- -j5

# 5. Run
./build/bin/vms_engine
# Expected: "vms_engine v1.0.0 — scaffold build OK"

# 6. Verify no lantana references in source
grep -r "lantana" CMakeLists.txt app/ core/ pipeline/ domain/ \
    infrastructure/ services/ 2>/dev/null || echo "PASS: No lantana references"
```

---

## Checklist

- [ ] Directory tree created per Section 1.1
- [ ] Root `CMakeLists.txt` with DeepStream 8.0, FetchContent (spdlog 1.14.1, yaml-cpp 0.8.0, hiredis 1.3.0, nlohmann_json 3.11.3)
- [ ] 6 sub-module `CMakeLists.txt` files (core, pipeline, domain, infrastructure, services, app)
- [ ] Placeholder `.cpp` files for each library target
- [ ] Stub `main.cpp` compiles and runs
- [ ] `configs/default.yml` exists (copy of canonical config)
- [ ] `.clang-format` in place
- [ ] `cmake configure` succeeds inside container
- [ ] `cmake build` succeeds inside container
- [ ] No `lantana` string in any project source file (except docs/)
