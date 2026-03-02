# VMS Engine — CMake Build System Reference

> **CMake 3.16+ · C++17 · Ninja or Make · NVIDIA DeepStream 8.0**  
> Hướng dẫn chi tiết về cấu trúc, cách sử dụng, và các pattern CMake trong dự án.

---

## Table of Contents

- [Tổng quan Build System](#1-tổng-quan-build-system)
- [Cấu trúc CMakeLists.txt](#2-cấu-trúc-cmakelists-files)
- [Configure & Build Commands](#3-configure--build-commands)
- [Build Types](#4-build-types)
- [Dependencies Management](#5-dependencies-management)
- [Target & Library System](#6-target--library-system)
- [Include Directories](#7-include-directories)
- [Compiler Flags & Warnings](#8-compiler-flags--warnings)
- [Conditional Compilation](#9-conditional-compilation)
- [Custom Targets](#10-custom-targets)
- [Generator Expressions](#11-generator-expressions)
- [FetchContent — Third-party Libraries](#12-fetchcontent--third-party-libraries)
- [Output Directories](#13-output-directories)
- [Post-Build Commands](#14-post-build-commands)
- [CMake Presets](#15-cmake-presets)
- [Common Errors & Fix](#16-common-errors--fix)
- [Anti-patterns to Avoid](#17-anti-patterns-to-avoid)

---

## 1. Tổng quan Build System

### Luồng build tổng quát

```
Root CMakeLists.txt
  ├── Compiler settings (C++17, warnings)
  ├── find_package / pkg_check_modules  ← tìm system deps
  ├── FetchContent_Declare / MakeAvailable ← tải third-party deps
  ├── add_subdirectory(core)
  ├── add_subdirectory(pipeline)
  ├── add_subdirectory(domain)
  ├── add_subdirectory(infrastructure)
  ├── add_subdirectory(services)
  └── add_subdirectory(app)  ← executable cuối cùng
```

Mỗi `add_subdirectory()` load file `CMakeLists.txt` của module đó và register các **CMake target** (thư viện hoặc executable).

### Nguyên tắc cốt lõi

| Nguyên tắc | Ý nghĩa |
|---|---|
| **INTERFACE / PUBLIC / PRIVATE** | Kiểm soát propagation của include dirs và link libs |
| **Target-based** (không dùng global variables) | Dùng `target_*` thay vì `include_directories()` global |
| **Dependency-ordered subdirectories** | Thư viện phụ thuộc phải được add trước |
| **Interface-first** | `core/` không phụ thuộc gì ngoài std + GStreamer forward-decl |

---

## 2. Cấu trúc CMakeLists Files

### Root `CMakeLists.txt` — nhiệm vụ

```cmake
cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(vms_engine VERSION 1.0.0 LANGUAGES CXX)

# 1. Compiler settings
# 2. Build type default
# 3. Output directories
# 4. Project options (features on/off)
# 5. find_package / pkg_check_modules
# 6. FetchContent (third-party)
# 7. Backend-specific dependencies
# 8. add_subdirectory(...)
# 9. Optional targets (format, tidy)
```

### Module `CMakeLists.txt` (pattern chuẩn)

```cmake
# --- Source files ---
set(SOURCES
    src/my_class.cpp
    src/another_class.cpp
)

# --- Library target ---
add_library(vms_engine_core STATIC ${SOURCES})

# --- Include directories ---
# PRIVATE: chỉ cần khi build LIB này
# PUBLIC:  cần cả khi build LIB này VÀ khi ai đó link vào nó
# INTERFACE: chỉ cần cho target khác khi link vào (không cần khi build chính nó)
target_include_directories(vms_engine_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include   # consumers của lib này cần include này
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src        # chỉ cần khi build lib này
)

# --- Link dependencies ---
target_link_libraries(vms_engine_core
    PUBLIC  spdlog::spdlog      # propagate đến consumer
    PRIVATE PkgConfig::GST      # chỉ cần khi build lib này
)

# --- C++ standard ---
target_compile_features(vms_engine_core PRIVATE cxx_std_17)
```

### Cấu trúc file theo các layers

```
vms-engine/
├── CMakeLists.txt              ← Root: global config, deps, add_subdirectory
├── app/
│   └── CMakeLists.txt          ← add_executable(vms_engine main.cpp)
├── core/
│   └── CMakeLists.txt          ← add_library(vms_engine_core INTERFACE)
├── pipeline/
│   └── CMakeLists.txt          ← add_library(vms_engine_pipeline STATIC ...)
├── domain/
│   └── CMakeLists.txt          ← add_library(vms_engine_domain STATIC ...)
├── infrastructure/
│   ├── CMakeLists.txt          ← add_subdirectory cho từng sub-module
│   ├── config_parser/
│   │   └── CMakeLists.txt
│   ├── messaging/
│   │   └── CMakeLists.txt
│   └── storage/
│       └── CMakeLists.txt
└── services/
    └── CMakeLists.txt
```

---

## 3. Configure & Build Commands

### Commands cơ bản

```bash
# === Configure ===

# Ninja (nhanh hơn make, khuyến nghị)
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream \
    -G Ninja

# GNU Make (fallback)
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream

# === Build ===

cmake --build build -- -j$(nproc)         # parallel build tất cả targets
cmake --build build --target vms_engine   # chỉ build executable
cmake --build build --target vms_engine_core  # chỉ build thư viện
cmake --build build -- -j$(nproc) -v      # verbose output (xem full compile commands)

# === Clean ===

cmake --build build --target clean        # xóa build artifacts
rm -rf build                              # clean hoàn toàn (cần configure lại)
```

### In-container workflow (dev environment)

```bash
# Bên trong container tại /opt/lantana (hoặc /opt/vms-engine)

# Configure lần đầu
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -G Ninja

# Build nhanh (incremental)
cmake --build build -- -j$(nproc)

# Chạy binary
./build/bin/vms_engine -c configs/default.yml

# Xem compile commands (dùng với clangd LSP)
cat build/compile_commands.json | head -50
```

### Useful cmake flags

| Flag | Ý nghĩa |
|---|---|
| `-DCMAKE_BUILD_TYPE=Debug` | Debug symbols, no optimizations (`-g -O0`) |
| `-DCMAKE_BUILD_TYPE=Release` | Full optimizations (`-O3 -DNDEBUG`) |
| `-DCMAKE_BUILD_TYPE=RelWithDebInfo` | Optimized + debug symbols |
| `-G Ninja` | Dùng Ninja generator (nhanh hơn make ~2x) |
| `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` | Tạo `compile_commands.json` cho LSP (clangd) |
| `-DCMAKE_VERBOSE_MAKEFILE=ON` | In full compile commands (debug build issues) |
| `--fresh` | Force reconfigure (CMake 3.24+) |

---

## 4. Build Types

### Bốn build types chuẩn

```cmake
# Root CMakeLists.txt — set default nếu user không chỉ định
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Debug' as none was specified.")
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
    # Liệt kê các lựa chọn trong cmake-gui
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()
```

| Type | Flags (GCC/Clang) | Dùng khi |
|---|---|---|
| `Debug` | `-g -O0` | Development — symbols đầy đủ, dễ debug với GDB |
| `Release` | `-O3 -DNDEBUG` | Production — tối ưu tối đa, tắt assert |
| `RelWithDebInfo` | `-O2 -g -DNDEBUG` | Profiling — optimized nhưng giữ symbols |
| `MinSizeRel` | `-Os -DNDEBUG` | Embedded — tối ưu size |

### Custom flags per type

```cmake
# Thêm sanitizers cho Debug
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fsanitize=address,undefined"
    CACHE STRING "Debug flags" FORCE)

# Tắt exceptions trong Release nếu không cần
# set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -fno-exceptions")
```

### Kiểm tra build type trong code

```cpp
// Trong C++ code
#ifndef NDEBUG
    LOG_D("Debug-only log: frame_count={}", count);
#endif

// Hoặc dùng CMake-defined macro:
// target_compile_definitions(my_target PRIVATE
//     $<$<CONFIG:Debug>:MY_DEBUG_CHECKS>)
```

---

## 5. Dependencies Management

### 5.1 System dependencies — `find_package`

```cmake
# Tìm thư viện system chuẩn (có FindXxx.cmake module)
find_package(Threads REQUIRED)          # pthreads → Threads::Threads
find_package(CUDA)                      # CUDA Toolkit → không bắt buộc (REQUIRED optional)
find_package(CUDAToolkit REQUIRED)      # CUDA runtime → CUDAToolkit::cudart

# Tìm với version constraint
find_package(Boost 1.71 REQUIRED COMPONENTS filesystem system)

# Import targets được tạo:
target_link_libraries(my_target PRIVATE Threads::Threads CUDAToolkit::cudart)
```

### 5.2 System dependencies — `pkg_check_modules`

```cmake
find_package(PkgConfig REQUIRED)

# GStreamer — nhiều modules cùng lúc
pkg_check_modules(GST REQUIRED IMPORTED_TARGET
    gstreamer-1.0>=1.14
    gstreamer-base-1.0>=1.14
    gstreamer-video-1.0>=1.14
    gstreamer-app-1.0>=1.14
    gstreamer-rtsp-1.0>=1.14
)

# GLib
pkg_check_modules(GLIB2 REQUIRED IMPORTED_TARGET
    glib-2.0>=2.56
    gobject-2.0>=2.56
    gio-2.0>=2.56
)

# libcurl
pkg_check_modules(CURL REQUIRED libcurl)

# Sử dụng IMPORTED_TARGET → dùng được tên target PkgConfig::GST
target_link_libraries(my_lib PRIVATE PkgConfig::GST PkgConfig::GLIB2)
```

### 5.3 Manual library discovery — `find_library` / `find_path`

Dùng cho các SDK không có pkg-config (như DeepStream):

```cmake
set(DEEPSTREAM_DIR "/opt/nvidia/deepstream/deepstream"
    CACHE PATH "Path to DeepStream installation")

# Kiểm tra header tồn tại trước khi link
if(NOT EXISTS "${DEEPSTREAM_DIR}/sources/includes/nvdsgstutils.h")
    message(FATAL_ERROR "DeepStream not found at: ${DEEPSTREAM_DIR}")
endif()

# Tìm từng thư viện
find_library(NVDS_META_LIB     nvds_meta      HINTS ${DEEPSTREAM_DIR}/lib REQUIRED)
find_library(NVDSGST_META_LIB  nvdsgst_meta   HINTS ${DEEPSTREAM_DIR}/lib REQUIRED)
find_library(NVDSGST_UTILS_LIB nvdsgstutils   HINTS ${DEEPSTREAM_DIR}/lib REQUIRED)
find_library(NVDS_OSD_LIB      nvosd          HINTS ${DEEPSTREAM_DIR}/lib REQUIRED)
find_library(NVBUFSURFACE_LIB  nvbufsurface   HINTS ${DEEPSTREAM_DIR}/lib REQUIRED)

# Tập hợp thành list
set(DEEPSTREAM_LIBS
    ${NVDS_META_LIB}
    ${NVDSGST_META_LIB}
    ${NVDSGST_UTILS_LIB}
    ${NVDS_OSD_LIB}
    ${NVBUFSURFACE_LIB}
)

# Include path của DeepStream SDK
set(DEEPSTREAM_INCLUDE_DIRS "${DEEPSTREAM_DIR}/sources/includes")
```

### 5.4 FetchContent — tải third-party tự động

```cmake
include(FetchContent)

# spdlog — structured logging
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1
)
set(SPDLOG_BUILD_PIC ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)
# Tạo target: spdlog::spdlog

# yaml-cpp — YAML parser
FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        0.8.0
)
FetchContent_MakeAvailable(yaml-cpp)
# Tạo target: yaml-cpp::yaml-cpp

# hiredis — Redis client
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build hiredis as static library" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    hiredis
    GIT_REPOSITORY https://github.com/redis/hiredis.git
    GIT_TAG        v1.3.0
)
FetchContent_MakeAvailable(hiredis)
# Tạo target: hiredis::hiredis

# nlohmann/json — JSON parsing
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)
# Tạo target: nlohmann_json::nlohmann_json
```

> **FetchContent lưu source tại**: `build/_deps/<name>-src/`  
> **Lần đầu configure**: cần internet để clone.  
> **Lần tiếp theo**: dùng cached source (nhanh).

### Dependency overview

```
vms_engine (executable)
  ├── vms_engine_core      (INTERFACE — headers only)
  ├── vms_engine_pipeline  (STATIC)
  │   ├── PkgConfig::GST
  │   ├── DeepStream SDK libs
  │   └── CUDAToolkit::cudart
  ├── vms_engine_domain    (STATIC)
  ├── vms_engine_infra_*   (STATIC — config_parser, messaging, storage)
  ├── vms_engine_services  (STATIC)
  ├── spdlog::spdlog
  ├── yaml-cpp::yaml-cpp
  ├── hiredis::hiredis
  ├── PkgConfig::GLIB2
  ├── Threads::Threads
  └── nlohmann_json::nlohmann_json
```

---

## 6. Target & Library System

### Loại targets

```cmake
# STATIC: tạo .a (archive) — linked at compile time
add_library(vms_engine_pipeline STATIC ${PIPELINE_SOURCES})

# SHARED: tạo .so (shared object) — linked at runtime (ít dùng trong project này)
add_library(vms_engine_plugin SHARED ${PLUGIN_SOURCES})

# INTERFACE: không có source files — chỉ có headers (header-only libraries)
add_library(vms_engine_core INTERFACE)

# OBJECT: tạo .o files, không link thành lib — dùng để share objects giữa nhiều targets
add_library(common_objs OBJECT src/common.cpp)
```

### STATIC vs INTERFACE cho core

Do `core/` chỉ chứa headers (interfaces + config types + utils), nên dùng `INTERFACE`:

```cmake
# core/CMakeLists.txt
add_library(vms_engine_core INTERFACE)

target_include_directories(vms_engine_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(vms_engine_core INTERFACE
    spdlog::spdlog      # consumers của core sẽ tự động có spdlog
    PkgConfig::GLIB2
)
```

### Visibility (PUBLIC / PRIVATE / INTERFACE)

```cmake
target_include_directories(my_lib
    PUBLIC   include/     # cần khi build my_lib VÀ khi ai link vào my_lib
    PRIVATE  src/internal # chỉ cần khi build my_lib
    INTERFACE deps/       # chỉ cần khi ai link vào my_lib (không cần khi build my_lib)
)

target_link_libraries(my_lib
    PUBLIC  vms_engine_core   # propagate: bất kỳ ai link my_lib sẽ tự có core
    PRIVATE yaml-cpp::yaml-cpp # không propagate: chỉ my_lib cần
)
```

**Rule of thumb:**
- `PUBLIC` → các consumer của lib **sẽ cần** sử dụng dependency này trong code của họ
- `PRIVATE` → dependency chỉ dùng trong **implementation** của lib, không expose ra public API
- `INTERFACE` → lib không tự dùng nhưng **consumer sẽ cần** (thường cho header-only)

### Alias targets (tên đẹp hơn)

```cmake
# Tạo alias để dùng namespace-style (giống find_package behavior)
add_library(engine::core ALIAS vms_engine_core)
add_library(engine::pipeline ALIAS vms_engine_pipeline)

# Consumer dùng alias (dễ đọc hơn)
target_link_libraries(vms_engine PRIVATE engine::core engine::pipeline)
```

---

## 7. Include Directories

### Phân biệt `include_directories` vs `target_include_directories`

```cmake
# ❌ KHÔNG DÙNG — global, ảnh hưởng tất cả targets trong dir và subdirs
include_directories(${DEEPSTREAM_INCLUDE_DIRS})

# ✅ DÙNG — scoped theo target
target_include_directories(vms_engine_pipeline PRIVATE ${DEEPSTREAM_INCLUDE_DIRS})
```

### Pattern chuẩn cho module include

```
pipeline/
├── include/
│   └── engine/
│       └── pipeline/
│           ├── builders/
│           │   └── infer_builder.hpp
│           └── probes/
│               └── object_probe.hpp
└── src/
    └── builders/
        └── infer_builder.cpp
```

```cmake
# pipeline/CMakeLists.txt
target_include_directories(vms_engine_pipeline
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include   # expose engine/pipeline/* headers
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src        # internal headers (nếu có)
)
```

```cpp
// Trong bất kỳ file nào link vào vms_engine_pipeline:
#include "engine/pipeline/builders/infer_builder.hpp"  // ← clean include path
```

### Thêm include dirs của SDK

```cmake
# Thêm DeepStream includes chỉ cho pipeline module
target_include_directories(vms_engine_pipeline PRIVATE
    ${DEEPSTREAM_INCLUDE_DIRS}      # /opt/nvidia/deepstream/deepstream/sources/includes
    ${CUDA_INCLUDE_DIRS}            # /usr/local/cuda/include
)
```

---

## 8. Compiler Flags & Warnings

### Warning flags chuẩn

```cmake
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vms_engine_pipeline PRIVATE
        -Wall           # most warnings
        -Wextra         # extra warnings
        -Wpedantic      # strict standard conformance
        -Wshadow        # local vars shadowing outer scope
        -Wformat=2      # format string checking
        -Wunused        # unused variables/functions
        # Cân nhắc thêm (thắt chặt hơn):
        # -Wconversion              # implicit conversions
        # -Werror                   # treat warnings as errors (strict mode)
        # -Wnon-virtual-dtor        # base class without virtual destructor
        # -Wold-style-cast          # C-style casts
    )
endif()
```

### Link Time Optimization (LTO) — Release only

```cmake
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)

if(ipo_supported AND CMAKE_BUILD_TYPE STREQUAL "Release")
    set_property(TARGET vms_engine_pipeline PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    message(STATUS "LTO/IPO enabled for Release build")
endif()
```

### Position Independent Code (PIC) — cần cho .so và static libs linked into .so

```cmake
# Bật PIC cho toàn bộ project
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Hoặc per-target
set_target_properties(vms_engine_pipeline PROPERTIES POSITION_INDEPENDENT_CODE ON)
```

---

## 9. Conditional Compilation

### CMake options

```cmake
# Root CMakeLists.txt — khai báo options
option(VMS_WITH_DEEPSTREAM   "Enable NVIDIA DeepStream support" ON)
option(VMS_WITH_KAFKA        "Enable Kafka messaging support"   OFF)
option(VMS_WITH_S3           "Enable AWS S3 storage support"    OFF)
option(VMS_ENABLE_TESTS      "Build unit and integration tests" OFF)

# Truyền vào code qua preprocessor macro
if(VMS_WITH_DEEPSTREAM)
    target_compile_definitions(vms_engine_pipeline PRIVATE VMS_WITH_DEEPSTREAM)
endif()
if(VMS_WITH_KAFKA)
    target_compile_definitions(vms_engine_infra_messaging PRIVATE VMS_WITH_KAFKA)
    find_package(RdKafka REQUIRED)
    target_link_libraries(vms_engine_infra_messaging PRIVATE RdKafka::rdkafka++)
endif()
```

### Dùng trong C++ code

```cpp
#ifdef VMS_WITH_DEEPSTREAM
    #include "engine/pipeline/builders/infer_builder.hpp"
    pipeline_ = std::make_unique<PipelineManager>(config);
#else
    #error "VMS Engine requires DeepStream. Enable VMS_WITH_DEEPSTREAM."
#endif

#ifdef VMS_WITH_KAFKA
    producer_ = std::make_unique<KafkaProducer>(broker_config);
#else
    producer_ = std::make_unique<RedisProducer>(redis_config);
#endif
```

### Conditional subdirectory (add only when feature enabled)

```cmake
# infrastructure/CMakeLists.txt
add_subdirectory(config_parser)   # luôn build
add_subdirectory(messaging)       # luôn build

if(VMS_WITH_S3)
    add_subdirectory(storage_s3)
else()
    add_subdirectory(storage_local)
endif()

if(VMS_ENABLE_TESTS)
    add_subdirectory(tests)
endif()
```

---

## 10. Custom Targets

### Format target — chạy `clang-format`

```cmake
find_program(CLANG_FORMAT_EXE clang-format)
if(CLANG_FORMAT_EXE)
    file(GLOB_RECURSE ALL_SOURCE_FILES
        "${CMAKE_SOURCE_DIR}/app/*.cpp"
        "${CMAKE_SOURCE_DIR}/core/include/**/*.hpp"
        "${CMAKE_SOURCE_DIR}/pipeline/include/**/*.hpp"
        "${CMAKE_SOURCE_DIR}/pipeline/src/**/*.cpp"
        "${CMAKE_SOURCE_DIR}/domain/**/*.hpp"
        "${CMAKE_SOURCE_DIR}/domain/**/*.cpp"
        "${CMAKE_SOURCE_DIR}/infrastructure/**/*.hpp"
        "${CMAKE_SOURCE_DIR}/infrastructure/**/*.cpp"
    )

    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE} -i --style=file ${ALL_SOURCE_FILES}
        COMMENT "Running clang-format..."
        VERBATIM
    )
    message(STATUS "Added 'format' target: cmake --build build --target format")
else()
    message(WARNING "clang-format not found — 'format' target unavailable")
endif()
```

### Tidy target — chạy `clang-tidy`

```cmake
find_program(CLANG_TIDY_EXE clang-tidy)
if(CLANG_TIDY_EXE AND CMAKE_EXPORT_COMPILE_COMMANDS)
    add_custom_target(tidy
        COMMAND ${CLANG_TIDY_EXE}
            -p ${CMAKE_BINARY_DIR}
            --header-filter=.*/engine/.*
            --fix
            ${ALL_SOURCE_FILES}
        COMMENT "Running clang-tidy..."
        VERBATIM
    )
    message(STATUS "Added 'tidy' target: cmake --build build --target tidy")
endif()
```

### DOT graph target — visualize GStreamer pipeline

```cmake
find_program(DOT_EXE dot)  # graphviz
if(DOT_EXE)
    add_custom_target(pipeline_graph
        COMMAND ${DOT_EXE} -Tpng
            ${CMAKE_SOURCE_DIR}/docs/configs/the_old_build_graph.dot
            -o ${CMAKE_BINARY_DIR}/pipeline_graph.png
        COMMENT "Generating pipeline graph PNG..."
        VERBATIM
    )
endif()
```

### Chạy custom targets

```bash
cmake --build build --target format         # format code
cmake --build build --target tidy           # static analysis
cmake --build build --target pipeline_graph # generate graph
```

---

## 11. Generator Expressions

Generator expressions được evaluate **lúc generate** (sau configure), không phải lúc configure.  
Dùng cho logic phụ thuộc vào build type, compiler, hoặc target properties.

### Syntax cơ bản

```cmake
$<CONDITION:value>                    # nếu CONDITION true → value, ngược lại → ""
$<IF:CONDITION,true_val,false_val>    # if-else
$<$<CONFIG:Debug>:value>              # value chỉ trong Debug build
$<TARGET_FILE:my_target>              # full path đến output file của target
$<TARGET_FILE_DIR:my_target>          # directory chứa output file
```

### Ví dụ thực tế

```cmake
# Link thư viện khác nhau theo build type
target_link_libraries(vms_engine PRIVATE
    $<$<CONFIG:Debug>:asan_lib>       # Address Sanitizer chỉ trong Debug
    $<$<CONFIG:Release>:tcmalloc>     # tcmalloc chỉ trong Release
)

# Compiler-specific flags
target_compile_options(vms_engine_pipeline PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wno-missing-field-initializers>
    $<$<CXX_COMPILER_ID:Clang>:-Wno-unknown-warning-option>
)

# Include dir có điều kiện theo feature flag
target_include_directories(vms_engine_app PRIVATE
    $<$<BOOL:${VMS_WITH_DEEPSTREAM}>:${DEEPSTREAM_INCLUDE_DIRS}>
)

# Post-build copy đến output directory của target
add_custom_command(TARGET vms_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/configs
        $<TARGET_FILE_DIR:vms_engine>/config    # ← generator expression
    COMMENT "Copying configs to build output"
)
```

### Build type checks

```cmake
# Chỉ bật sanitizers trong Debug
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(vms_engine_pipeline PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(vms_engine PRIVATE
        -fsanitize=address,undefined
    )
endif()
```

---

## 12. FetchContent — Third-party Libraries

### Pattern đầy đủ

```cmake
include(FetchContent)

# Bước 1: Declare (metadata)
FetchContent_Declare(
    <name>                          # tên để refer đến
    GIT_REPOSITORY <url>            # Git URL
    GIT_TAG        <tag/commit>     # pin version để reproducible builds
    # GIT_SHALLOW TRUE              # chỉ lấy latest commit (nhanh hơn, không có history)
    # SOURCE_DIR <path>             # override download location
)

# Bước 2: Make available (download + configure)
FetchContent_MakeAvailable(<name>)
# → nguồn được tải về build/_deps/<name>-src/
# → tạo CMake targets của thư viện
```

### Set options TRƯỚC MakeAvailable

```cmake
# ✅ ĐÚNG: set options trước FetchContent_MakeAvailable
set(SPDLOG_BUILD_PIC ON CACHE BOOL "" FORCE)
set(SPDLOG_ENABLE_PCH ON CACHE BOOL "" FORCE)    # Precompiled headers
FetchContent_MakeAvailable(spdlog)

# ❌ SAI: set options sau (quá muộn, không có effect)
FetchContent_MakeAvailable(spdlog)
set(SPDLOG_BUILD_PIC ON CACHE BOOL "" FORCE)     # ignored!
```

### Multiple declarations cùng lúc + batch MakeAvailable

```cmake
FetchContent_Declare(spdlog    GIT_REPOSITORY ... GIT_TAG v1.14.1)
FetchContent_Declare(yaml-cpp  GIT_REPOSITORY ... GIT_TAG 0.8.0)
FetchContent_Declare(hiredis   GIT_REPOSITORY ... GIT_TAG v1.3.0)
FetchContent_Declare(nlohmann_json GIT_REPOSITORY ... GIT_TAG v3.11.3)

# Một lần MakeAvailable cho tất cả
FetchContent_MakeAvailable(spdlog yaml-cpp hiredis nlohmann_json)
```

### Override FetchContent với local source (dev mode)

```cmake
# Nếu bạn muốn dev trên bản local của spdlog thay vì download
set(FETCHCONTENT_SOURCE_DIR_SPDLOG /path/to/local/spdlog
    CACHE PATH "Use local spdlog source instead of downloading")
FetchContent_MakeAvailable(spdlog)  # sẽ dùng local source
```

---

## 13. Output Directories

### Cài đặt output dirs

```cmake
# Root CMakeLists.txt
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)  # .a files
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)  # .so files
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)  # executable + .dll
```

Sau khi build:
```
build/
├── bin/
│   └── vms_engine              ← executable
├── lib/
│   ├── libvms_engine_core.a
│   ├── libvms_engine_pipeline.a
│   └── ...
└── _deps/                      ← FetchContent sources
    ├── spdlog-src/
    └── yaml-cpp-src/
```

### Per-target output override

```cmake
# Đặt output của test binary vào thư mục riêng
set_target_properties(vms_engine_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)
```

---

## 14. Post-Build Commands

### Copy configs sau khi build

```cmake
# app/CMakeLists.txt
set(CONFIG_SRC  ${CMAKE_SOURCE_DIR}/configs)
set(CONFIG_DEST $<TARGET_FILE_DIR:vms_engine>/config)

add_custom_command(TARGET vms_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CONFIG_SRC} ${CONFIG_DEST}
    COMMENT "Copying configs to output directory"
    VERBATIM
)
```

### Copy models sau khi build

```cmake
set(MODELS_SRC  ${CMAKE_SOURCE_DIR}/models)
set(MODELS_DEST $<TARGET_FILE_DIR:vms_engine>/models)

if(EXISTS ${MODELS_SRC})
    add_custom_command(TARGET vms_engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${MODELS_SRC} ${MODELS_DEST}
        COMMENT "Copying models to output directory"
        VERBATIM
    )
else()
    message(WARNING "Models directory not found: ${MODELS_SRC}")
endif()
```

### Tạo symlink cho compile_commands.json (dùng với clangd)

```cmake
# Tạo symlink từ root → build/compile_commands.json
add_custom_target(compile_commands_symlink ALL
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
    DEPENDS ${CMAKE_BINARY_DIR}/compile_commands.json
    COMMENT "Creating compile_commands.json symlink at root"
)
```

---

## 15. CMake Presets

CMake Presets (`CMakePresets.json`) cho phép define cấu hình build được version-controlled và  
shared với team. CMake 3.19+.

### `CMakePresets.json` mẫu cho vms-engine

```json
{
    "version": 6,
    "configurePresets": [
        {
            "name": "debug",
            "hidden": false,
            "description": "Debug build with sanitizers",
            "binaryDir": "${sourceDir}/build/debug",
            "generator": "Ninja",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE":              "Debug",
                "CMAKE_EXPORT_COMPILE_COMMANDS": "ON",
                "DEEPSTREAM_DIR": "/opt/nvidia/deepstream/deepstream"
            }
        },
        {
            "name": "release",
            "hidden": false,
            "description": "Optimized release build",
            "binaryDir": "${sourceDir}/build/release",
            "generator": "Ninja",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE":              "Release",
                "CMAKE_EXPORT_COMPILE_COMMANDS": "OFF",
                "DEEPSTREAM_DIR": "/opt/nvidia/deepstream/deepstream"
            }
        },
        {
            "name": "relwithdebinfo",
            "description": "Release with debug info (profiling)",
            "binaryDir": "${sourceDir}/build/profile",
            "generator": "Ninja",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE":              "RelWithDebInfo",
                "CMAKE_EXPORT_COMPILE_COMMANDS": "ON",
                "DEEPSTREAM_DIR": "/opt/nvidia/deepstream/deepstream"
            }
        }
    ],
    "buildPresets": [
        {
            "name": "debug",
            "configurePreset": "debug",
            "jobs": 0
        },
        {
            "name": "release",
            "configurePreset": "release",
            "jobs": 0
        }
    ]
}
```

### Sử dụng presets

```bash
# List available presets
cmake --list-presets

# Configure với preset
cmake --preset debug
cmake --preset release

# Build với preset
cmake --build --preset debug
cmake --build --preset release

# Hoặc kết hợp
cmake --preset debug && cmake --build --preset debug
```

---

## 16. Common Errors & Fix

### `Could not find module FindXxx.cmake`

```
CMake Error: By not providing "FindDeepStream.cmake" in CMAKE_MODULE_PATH
```

**Fix**: Dùng `find_library` + `find_path` thủ công thay vì `find_package(DeepStream)`:
```cmake
find_library(NVDS_META_LIB nvds_meta HINTS ${DEEPSTREAM_DIR}/lib REQUIRED)
find_path(NVDS_INCLUDE_DIR nvdsgstutils.h HINTS ${DEEPSTREAM_DIR}/sources/includes REQUIRED)
```

### `undefined reference to symbol`

```
undefined reference to `gst_element_factory_make'
```

**Fix**: Thiếu link dependency. Kiểm tra `target_link_libraries`:
```cmake
target_link_libraries(my_target PRIVATE PkgConfig::GST)  # thêm GST
```

Hoặc thư viện cần được link theo đúng thứ tự (linker đọc left-to-right):
```cmake
# ❌ SAI thứ tự — libA cần libB nhưng libB đến trước
target_link_libraries(exec PRIVATE libA libB)  # có thể fail

# ✅ ĐÚNG thứ tự
target_link_libraries(exec PRIVATE libB libA)
```

### `FetchContent failed (network unavailable)`

```
CMake Error: Failed to download repository
network error: couldn't connect to server
```

**Fix inside container**:
```bash
# Kiểm tra DNS
ping github.com

# Nếu không có internet, pre-clone và dùng local source:
set(FETCHCONTENT_SOURCE_DIR_SPDLOG /opt/deps/spdlog CACHE PATH "")
```

### `double free` / `gst_object_unref: assertion 'object->ref_count > 0'`

Triệu chứng của việc quên `release()` sau `gst_bin_add()`:
```cmake
# Không phải CMake lỗi — xem RAII.md #11 anti-patterns
```

### Include paths không được tìm thấy

```
fatal error: engine/pipeline/builders/infer_builder.hpp: No such file or directory
```

**Fix**: Kiểm tra `target_include_directories` visibility:
```cmake
# Nếu consumer của lib không tìm được header → đổi từ PRIVATE sang PUBLIC
target_include_directories(vms_engine_pipeline
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include  # ← thay PRIVATE bằng PUBLIC
)
```

### `CUDA not found` trong container

```
CMake Error: find_package(CUDAToolkit) failed
```

**Fix**:
```bash
# Kiểm tra CUDA trong container
nvidia-smi
nvcc --version

# Nếu thiếu PATH
export PATH=/usr/local/cuda/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH

# Hoặc chỉ định rõ trong CMake
cmake -S . -B build -DCMAKE_CUDA_COMPILER=/usr/local/cuda/bin/nvcc
```

---

## 17. Anti-patterns to Avoid

```cmake
# ❌ Global include_directories — ảnh hưởng tất cả targets
include_directories(${DEEPSTREAM_INCLUDE_DIRS})  # BAD

# ✅ Scoped per target
target_include_directories(vms_engine_pipeline PRIVATE ${DEEPSTREAM_INCLUDE_DIRS})

# ❌ Hardcode paths
target_include_directories(my_lib PRIVATE /home/user/myproject/include)  # BAD

# ✅ CMake variables
target_include_directories(my_lib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ❌ link_libraries() global
link_libraries(spdlog::spdlog)  # BAD — ảnh hưởng mọi target sau đó

# ✅ target_link_libraries scoped
target_link_libraries(vms_engine_pipeline PRIVATE spdlog::spdlog)

# ❌ Không pin FetchContent version
FetchContent_Declare(spdlog GIT_REPOSITORY ... GIT_TAG main)  # BAD — không reproducible

# ✅ Pin exact version
FetchContent_Declare(spdlog GIT_REPOSITORY ... GIT_TAG v1.14.1)

# ❌ add_compile_options() global
add_compile_options(-Wall -Wextra)  # ảnh hưởng cả FetchContent dependencies

# ✅ target_compile_options scoped
target_compile_options(vms_engine_pipeline PRIVATE -Wall -Wextra)

# ❌ Thêm subdirectory không đúng thứ tự
add_subdirectory(app)       # app link vào core nhưng core chưa defined
add_subdirectory(core)      # ← ERROR: target vms_engine_core chưa tồn tại khi app cần

# ✅ Subdirectory theo thứ tự dependency
add_subdirectory(core)       # define targets trước
add_subdirectory(pipeline)
add_subdirectory(app)        # link vào core và pipeline đã defined

# ❌ Dùng CMAKE_SOURCE_DIR để refer đến file trong subdirectory
include(${CMAKE_SOURCE_DIR}/cmake/utils.cmake)  # fragile khi refactor

# ✅ Dùng CMAKE_CURRENT_SOURCE_DIR
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/utils.cmake)
```

---

## Quick Reference Cheat Sheet

```bash
# === Configure ===
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -G Ninja          # Debug + Ninja
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -G Ninja        # Release + Ninja
cmake --preset debug                                            # với CMakePresets.json

# === Build ===
cmake --build build -- -j$(nproc)                              # build all
cmake --build build --target vms_engine -- -j$(nproc)          # build executable only
cmake --build build -- -j$(nproc) -v                           # verbose
cmake --build build --target format                            # format code
cmake --build build --target tidy                              # clang-tidy

# === Info ===
cmake --build build --target help                              # list all targets
cmake -L build                                                 # list cache variables
cmake -LH build                                                # list cache variables with help

# === Clean ===
cmake --build build --target clean                             # clean artifacts
rm -rf build && cmake -S . -B build ...                        # full clean rebuild
```

---

*See also:*
- [`RAII.md`](RAII.md) — C++ resource management patterns
- [`ARCHITECTURE_BLUEPRINT.md`](ARCHITECTURE_BLUEPRINT.md) — overall architecture
- [`AGENTS.md`](../../AGENTS.md) — build commands quick reference
