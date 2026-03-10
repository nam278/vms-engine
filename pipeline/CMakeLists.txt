# ── Collect all .cpp sources recursively ────────────────────────────
file(GLOB_RECURSE PIPELINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_library(vms_engine_pipeline STATIC ${PIPELINE_SOURCES})

target_include_directories(vms_engine_pipeline
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${DEEPSTREAM_INCLUDE_DIRS}
    PRIVATE
        ${CUDAToolkit_INCLUDE_DIRS}
)

target_link_libraries(vms_engine_pipeline
    PUBLIC  vms_engine_core
    PRIVATE ${DEEPSTREAM_SDK_LIBS}
    PRIVATE PkgConfig::GST
    PRIVATE PkgConfig::GLIB2
    PRIVATE PkgConfig::CURL
    PRIVATE CUDA::cudart
    PRIVATE nlohmann_json::nlohmann_json
)

set_target_properties(vms_engine_pipeline PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)
