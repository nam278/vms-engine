add_library(vms_engine_domain STATIC
    src/runtime_param_rules.cpp
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
