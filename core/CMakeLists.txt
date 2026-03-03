add_library(vms_engine_core STATIC
    src/utils/spdlog_logger.cpp
    src/utils/uuid_v7_generator.cpp
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
