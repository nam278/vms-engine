# ── Config Parser ────────────────────────────────────────────────────
file(GLOB CONFIG_PARSER_SOURCES config_parser/src/*.cpp)
add_library(vms_engine_config_parser STATIC ${CONFIG_PARSER_SOURCES})

target_include_directories(vms_engine_config_parser
    PUBLIC  config_parser/include
    PRIVATE config_parser/src          # internal yaml_parser_helpers.hpp
)

target_link_libraries(vms_engine_config_parser
    PUBLIC  vms_engine_core
    PRIVATE yaml-cpp::yaml-cpp
)

set_target_properties(vms_engine_config_parser PROPERTIES
    CXX_STANDARD 17  CXX_STANDARD_REQUIRED ON)

# ── Messaging ────────────────────────────────────────────────────────
file(GLOB MESSAGING_SOURCES messaging/src/*.cpp)
add_library(vms_engine_messaging STATIC ${MESSAGING_SOURCES})

target_include_directories(vms_engine_messaging
    PUBLIC messaging/include
)

target_link_libraries(vms_engine_messaging
    PUBLIC  vms_engine_core
    PRIVATE hiredis
    PRIVATE rdkafka++       # librdkafka C++ API (FetchContent)
    PRIVATE nlohmann_json::nlohmann_json
)

set_target_properties(vms_engine_messaging PROPERTIES
    CXX_STANDARD 17  CXX_STANDARD_REQUIRED ON)

# ── Storage ──────────────────────────────────────────────────────────
file(GLOB STORAGE_SOURCES storage/src/*.cpp)
add_library(vms_engine_storage STATIC ${STORAGE_SOURCES})

target_include_directories(vms_engine_storage
    PUBLIC storage/include
)

target_link_libraries(vms_engine_storage
    PUBLIC  vms_engine_core
    PRIVATE PkgConfig::CURL
    PRIVATE ${CMAKE_DL_LIBS}        # for std::filesystem on older GCC
)

target_link_libraries(vms_engine_storage
    PRIVATE $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.1>>:stdc++fs>
)

set_target_properties(vms_engine_storage PROPERTIES
    CXX_STANDARD 17  CXX_STANDARD_REQUIRED ON)

# ── REST API (stub — Pistache not yet integrated) ────────────────────
file(GLOB REST_API_SOURCES rest_api/src/*.cpp)
add_library(vms_engine_rest_api STATIC ${REST_API_SOURCES})

target_include_directories(vms_engine_rest_api
    PUBLIC rest_api/include
)

target_link_libraries(vms_engine_rest_api
    PUBLIC vms_engine_core
)

set_target_properties(vms_engine_rest_api PROPERTIES
    CXX_STANDARD 17  CXX_STANDARD_REQUIRED ON)

# ── Unified facade target (what app/ links to) ──────────────────────
add_library(vms_engine_infrastructure INTERFACE)
target_link_libraries(vms_engine_infrastructure INTERFACE
    vms_engine_config_parser
    vms_engine_messaging
    vms_engine_storage
    vms_engine_rest_api
)
