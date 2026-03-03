add_executable(vms_engine main.cpp)

target_link_libraries(vms_engine
    PRIVATE vms_engine_core
    PRIVATE vms_engine_pipeline
    PRIVATE vms_engine_domain
    PRIVATE vms_engine_infrastructure
    PRIVATE Threads::Threads
)

# Copy configs/ to build/bin/configs/ on every build
add_custom_command(TARGET vms_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/configs
            $<TARGET_FILE_DIR:vms_engine>/configs
    COMMENT "Copying configs/ → build/bin/configs/"
    VERBATIM)

# Create logs/ directory alongside the binary
add_custom_command(TARGET vms_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:vms_engine>/logs
    COMMENT "Creating build/bin/logs/"
    VERBATIM)
