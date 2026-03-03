add_executable(vms_engine main.cpp)

target_link_libraries(vms_engine
    PRIVATE vms_engine_core
    PRIVATE vms_engine_pipeline
    PRIVATE vms_engine_domain
    PRIVATE vms_engine_infrastructure
    PRIVATE vms_engine_services
    PRIVATE Threads::Threads
)

# Copy config to build output
add_custom_command(TARGET vms_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_SOURCE_DIR}/configs
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/configs
    COMMENT "Linking configs/ → build/bin/configs/")
