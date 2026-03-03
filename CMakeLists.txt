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

pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)

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
             nvbufsurface nvbufsurftransform nvds_infer)
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

# Static build — binary must be fully self-contained for production image.
# Dockerfile.image only copies build/bin/, so no .so from build/lib/ is available
# at runtime. All FetchContent deps must link statically into vms_engine.
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1)

set(YAML_CPP_FORMAT_SOURCE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        0.8.0)

# BUILD_SHARED_LIBS OFF forces hiredis → .a; PIC required when linked into shared .so plugins
set(BUILD_SHARED_LIBS            OFF CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "" FORCE)
set(DISABLE_TESTS                ON  CACHE BOOL "" FORCE)
FetchContent_Declare(hiredis
    GIT_REPOSITORY https://github.com/redis/hiredis.git
    GIT_TAG        v1.3.0)

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3)

FetchContent_MakeAvailable(spdlog yaml-cpp hiredis nlohmann_json)

# ── Sub-modules ─────────────────────────────────────────────────────
add_subdirectory(core)
add_subdirectory(pipeline)
add_subdirectory(domain)
add_subdirectory(infrastructure)
add_subdirectory(plugins)
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
    add_custom_target(vms_format
        COMMAND ${CLANG_FORMAT} -i ${ALL_SOURCES}
        COMMENT "Running clang-format on vms-engine sources"
        VERBATIM)
endif()
