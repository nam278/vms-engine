add_library(vms_engine_services STATIC
    src/triton_inference_client.cpp
)

target_include_directories(vms_engine_services
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(vms_engine_services
    PUBLIC  vms_engine_core
    PRIVATE PkgConfig::CURL
)

set_target_properties(vms_engine_services PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)
