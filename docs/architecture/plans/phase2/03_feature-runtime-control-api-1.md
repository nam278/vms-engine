---
goal: Runtime control transports for pipeline element properties
version: 1.0
date_created: 2026-03-10
last_updated: 2026-03-10
owner: VMS Engine Team
status: In progress
tags:
  [feature, architecture, runtime-control, rest-api, redis, kafka, deepstream]
---

# Introduction

![Status: In progress](https://img.shields.io/badge/status-In%20progress-yellow)

Plan này mô tả implementation phase 2 cho runtime control của VMS Engine theo hướng transport-agnostic. Mục tiêu là expose một lõi handler ổn định theo `pipeline_id` và `element_id` để query state pipeline và cập nhật runtime-safe GObject properties của DeepStream elements khi pipeline đang chạy, sau đó gắn handler này vào cả HTTP API lẫn command transport generic qua Redis Streams hoặc Kafka. Use case đầu tiên là bật/tắt `nvdsosd.display-bbox` và `nvdsosd.display-text`.

## 1. Requirements & Constraints

- **REQ-001**: Tạo top-level YAML block `control_api:` với các field `enable`, `bind_address`, `port` trong [core/include/engine/core/config/config_types.hpp](/home/vms/Lantana/Dev/vms-engine/core/include/engine/core/config/config_types.hpp).
- **REQ-002**: Parser phải đọc `control_api:` qua [infrastructure/config_parser/include/engine/infrastructure/config_parser/yaml_config_parser.hpp](/home/vms/Lantana/Dev/vms-engine/infrastructure/config_parser/include/engine/infrastructure/config_parser/yaml_config_parser.hpp), [infrastructure/config_parser/src/yaml_config_parser.cpp](/home/vms/Lantana/Dev/vms-engine/infrastructure/config_parser/src/yaml_config_parser.cpp), và file parser section riêng.
- **REQ-003**: `PipelineManager` phải implement `IRuntimeParamManager` để runtime control đi qua core contract hiện có, không bypass bằng code đường tắt riêng cho REST adapter.
- **REQ-004**: Runtime property update phải lookup element theo `element_id` và normalize property alias dạng `display_bbox` về GStreamer property thực `display-bbox`.
- **REQ-005**: Runtime property update phải marshal vào GLib main context khi `GMainLoop` đang chạy.
- **REQ-006**: HTTP API phải expose tối thiểu các route `GET /health`, `GET /api/v1/pipelines/{pipeline_id}/state`, `GET /api/v1/pipelines/{pipeline_id}/elements/{element_id}/properties/{property}`, và `PATCH /api/v1/pipelines/{pipeline_id}/elements/{element_id}/properties`.
- **REQ-007**: HTTP API chỉ được cho phép đổi các runtime param nằm trong allowlist lấy từ `RuntimeParamRules::create_default()`.
- **REQ-008**: `RuntimeParamRules::create_default()` phải chứa `osd.display_bbox` và `osd.display_text` để use case OSD toggle đi qua cùng contract với các runtime param khác.
- **REQ-009**: Default dev sample config phải có `sources.id: sources` và `control_api:` bật sẵn trong [dev/configs/deepstream_default.yml](/home/vms/Lantana/Dev/vms-engine/dev/configs/deepstream_default.yml).
- **REQ-010**: Tạo top-level YAML block `control_messaging:` với các field `enable`, `channel`, `reply_channel`, và parse nó qua config parser.
- **REQ-011**: HTTP API và broker-based command consumer phải dùng chung một `RuntimeControlHandler`, không được nhân đôi logic validation/allowlist/set/get.
- **REQ-012**: Broker command phải dùng `IMessageConsumer`/`IMessageProducer` abstraction để có thể đổi giữa Redis Streams và Kafka qua `messaging.type`.
- **REQ-013**: Tài liệu runtime control phải phản ánh implementation thật trong [docs/architecture/deepstream/11_runtime_element_control.md](/home/vms/Lantana/Dev/vms-engine/docs/architecture/deepstream/11_runtime_element_control.md).
- **CON-001**: Chỉ dùng C++17.
- **CON-002**: Không thêm framework HTTP mới; tận dụng GLib/GIO đã có trong dependency graph.
- **CON-003**: Không dùng DeepStream embedded REST API cho runtime property control; API này chỉ dành cho dynamic source add/remove.
- **CON-004**: Không thay đổi topology pipeline để bật/tắt OSD; chỉ set property runtime trên element hiện có.
- **GUD-001**: Mỗi element phải có id ổn định từ config; source bin dùng `sources.id` thay vì tên cứng `nvmultiurisrcbin0`.
- **PAT-001**: HTTP, Redis, Kafka về sau phải cùng đi vào một lõi runtime control là `IRuntimeParamManager`.
- **PAT-002**: `app -> worker` có thể giữ HTTP/gRPC, còn `worker -> engine` nên dùng command transport generic; engine không được gắn chặt vào riêng Redis hay riêng Kafka.

## 2. Implementation Steps

### Implementation Phase 1

- GOAL-001: Chuẩn hóa config contract và runtime contract cho element-level property control.

| Task     | Description                                                                                                                                                                                                                                                                               | Completed | Date       |
| -------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------- | ---------- |
| TASK-001 | Thêm `ControlApiConfig` và `PipelineConfig.control_api` vào [core/include/engine/core/config/config_types.hpp](/home/vms/Lantana/Dev/vms-engine/core/include/engine/core/config/config_types.hpp).                                                                                        | ✅        | 2026-03-10 |
| TASK-002 | Thêm parser declaration `parse_control_api(...)` vào [infrastructure/config_parser/include/engine/infrastructure/config_parser/yaml_config_parser.hpp](/home/vms/Lantana/Dev/vms-engine/infrastructure/config_parser/include/engine/infrastructure/config_parser/yaml_config_parser.hpp). | ✅        | 2026-03-10 |
| TASK-003 | Dispatch `control_api:` trong [infrastructure/config_parser/src/yaml_config_parser.cpp](/home/vms/Lantana/Dev/vms-engine/infrastructure/config_parser/src/yaml_config_parser.cpp).                                                                                                        | ✅        | 2026-03-10 |
| TASK-004 | Tạo section parser [infrastructure/config_parser/src/yaml_parser_control_api.cpp](/home/vms/Lantana/Dev/vms-engine/infrastructure/config_parser/src/yaml_parser_control_api.cpp) với default `enable=false`, `bind_address=0.0.0.0`, `port=18080`.                                        | ✅        | 2026-03-10 |
| TASK-005 | Bổ sung rules `osd.display_bbox` và `osd.display_text` vào [domain/src/runtime_param_rules.cpp](/home/vms/Lantana/Dev/vms-engine/domain/src/runtime_param_rules.cpp).                                                                                                                     | ✅        | 2026-03-10 |

### Implementation Phase 2

- GOAL-002: Implement runtime property manager và HTTP control server.

| Task     | Description                                                                                                                                                                                                                                                                                                                                                                                      | Completed | Date       |
| -------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | --------- | ---------- |
| TASK-006 | Mở rộng [pipeline/include/engine/pipeline/pipeline_manager.hpp](/home/vms/Lantana/Dev/vms-engine/pipeline/include/engine/pipeline/pipeline_manager.hpp) để `PipelineManager` implement `IRuntimeParamManager`.                                                                                                                                                                                   | ✅        | 2026-03-10 |
| TASK-007 | Implement generic GObject property set/get với type introspection và main-context marshaling trong [pipeline/src/pipeline_manager.cpp](/home/vms/Lantana/Dev/vms-engine/pipeline/src/pipeline_manager.cpp).                                                                                                                                                                                      | ✅        | 2026-03-10 |
| TASK-008 | Thay REST stub bằng GIO HTTP server thật trong [infrastructure/rest_api/include/engine/infrastructure/rest_api/pistache_server.hpp](/home/vms/Lantana/Dev/vms-engine/infrastructure/rest_api/include/engine/infrastructure/rest_api/pistache_server.hpp) và [infrastructure/rest_api/src/pistache_server.cpp](/home/vms/Lantana/Dev/vms-engine/infrastructure/rest_api/src/pistache_server.cpp). | ✅        | 2026-03-10 |
| TASK-009 | Link JSON support cho rest_api target trong [infrastructure/CMakeLists.txt](/home/vms/Lantana/Dev/vms-engine/infrastructure/CMakeLists.txt).                                                                                                                                                                                                                                                     | ✅        | 2026-03-10 |

### Implementation Phase 3

- GOAL-003: Wire runtime API vào application lifecycle và cập nhật sample/docs.

| Task     | Description                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              | Completed | Date       |
| -------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------- | ---------- |
| TASK-010 | Wire control API startup/shutdown trong [app/main.cpp](/home/vms/Lantana/Dev/vms-engine/app/main.cpp), gồm allowlist lấy từ `RuntimeParamRules::create_default()`.                                                                                                                                                                                                                                                                                                                                                                                                                                       | ✅        | 2026-03-10 |
| TASK-011 | Bật sample `control_api:` trong [dev/configs/deepstream_default.yml](/home/vms/Lantana/Dev/vms-engine/dev/configs/deepstream_default.yml) và [docs/configs/deepstream_default.yml](/home/vms/Lantana/Dev/vms-engine/docs/configs/deepstream_default.yml).                                                                                                                                                                                                                                                                                                                                                | ✅        | 2026-03-10 |
| TASK-012 | Cập nhật tài liệu runtime control trong [docs/architecture/deepstream/11_runtime_element_control.md](/home/vms/Lantana/Dev/vms-engine/docs/architecture/deepstream/11_runtime_element_control.md).                                                                                                                                                                                                                                                                                                                                                                                                       | ✅        | 2026-03-10 |
| TASK-013 | Thêm `ControlMessagingConfig` và `PipelineConfig.control_messaging` vào [core/include/engine/core/config/config_types.hpp](/home/vms/Lantana/Dev/vms-engine/core/include/engine/core/config/config_types.hpp).                                                                                                                                                                                                                                                                                                                                                                                           | ✅        | 2026-03-10 |
| TASK-014 | Parse `control_messaging:` trong [infrastructure/config_parser/include/engine/infrastructure/config_parser/yaml_config_parser.hpp](/home/vms/Lantana/Dev/vms-engine/infrastructure/config_parser/include/engine/infrastructure/config_parser/yaml_config_parser.hpp), [infrastructure/config_parser/src/yaml_config_parser.cpp](/home/vms/Lantana/Dev/vms-engine/infrastructure/config_parser/src/yaml_config_parser.cpp), và [infrastructure/config_parser/src/yaml_parser_control_messaging.cpp](/home/vms/Lantana/Dev/vms-engine/infrastructure/config_parser/src/yaml_parser_control_messaging.cpp). | ✅        | 2026-03-10 |
| TASK-015 | Tạo shared runtime handler trong [infrastructure/control/include/engine/infrastructure/control/runtime_control_handler.hpp](/home/vms/Lantana/Dev/vms-engine/infrastructure/control/include/engine/infrastructure/control/runtime_control_handler.hpp) và [infrastructure/control/src/runtime_control_handler.cpp](/home/vms/Lantana/Dev/vms-engine/infrastructure/control/src/runtime_control_handler.cpp).                                                                                                                                                                                             | ✅        | 2026-03-10 |
| TASK-016 | Refactor [infrastructure/rest_api/src/pistache_server.cpp](/home/vms/Lantana/Dev/vms-engine/infrastructure/rest_api/src/pistache_server.cpp) để dùng chung `RuntimeControlHandler`.                                                                                                                                                                                                                                                                                                                                                                                                                      | ✅        | 2026-03-10 |
| TASK-017 | Tạo broker-based command consumer trong [infrastructure/control/include/engine/infrastructure/control/runtime_control_message_consumer.hpp](/home/vms/Lantana/Dev/vms-engine/infrastructure/control/include/engine/infrastructure/control/runtime_control_message_consumer.hpp) và [infrastructure/control/src/runtime_control_message_consumer.cpp](/home/vms/Lantana/Dev/vms-engine/infrastructure/control/src/runtime_control_message_consumer.cpp).                                                                                                                                                  | ✅        | 2026-03-10 |
| TASK-018 | Wire control messaging startup/shutdown trong [app/main.cpp](/home/vms/Lantana/Dev/vms-engine/app/main.cpp) với consumer riêng, không reuse evidence consumer.                                                                                                                                                                                                                                                                                                                                                                                                                                           | ✅        | 2026-03-10 |
| TASK-019 | Bật sample `control_messaging:` trong [dev/configs/deepstream_default.yml](/home/vms/Lantana/Dev/vms-engine/dev/configs/deepstream_default.yml) và [docs/configs/deepstream_default.yml](/home/vms/Lantana/Dev/vms-engine/docs/configs/deepstream_default.yml).                                                                                                                                                                                                                                                                                                                                          | ✅        | 2026-03-10 |
| TASK-020 | Chạy full CMake build để xác minh compile/link toàn repo.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |           |            |
| TASK-021 | Chạy smoke test HTTP với `GET /health`, `GET /state`, và `PATCH` toggle OSD trên pipeline thực.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |           |            |
| TASK-022 | Chạy smoke test broker command `set_element_properties` qua Redis Streams hoặc Kafka và xác minh reply payload.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |           |            |

## 3. Alternatives

- **ALT-001**: Dùng DeepStream embedded REST API để đổi `nvdsosd` properties. Không chọn vì API đó chỉ dành cho dynamic add/remove stream của `nvmultiurisrcbin`.
- **ALT-002**: Tích hợp Pistache thật vào CMake. Không chọn cho phase này vì repo chưa có dependency đó, trong khi GLib/GIO đã sẵn và đủ cho API control nhỏ.
- **ALT-003**: Dùng Redis Streams/Kafka làm control path duy nhất và bỏ HTTP hoàn toàn. Không chọn vì worker/app vẫn cần sync semantics tốt cho orchestration, debug, và fallback direct-to-engine.
- **ALT-004**: Chỉ special-case `osd.display_bbox` và `osd.display_text` bằng code cứng. Không chọn vì runtime manager generic bằng GObject introspection mở rộng tốt hơn cho element khác.

## 4. Dependencies

- **DEP-001**: `PkgConfig::GLIB2` trong [CMakeLists.txt](/home/vms/Lantana/Dev/vms-engine/CMakeLists.txt) đã bao gồm `gio-2.0` để dùng `GSocketService`.
- **DEP-002**: `nlohmann_json::nlohmann_json` đã có trong workspace dependencies và được link thêm vào rest_api target.
- **DEP-003**: `PipelineManager` phải được giữ sống suốt lifetime của `RuntimeControlHandler` vì handler giữ raw pointers sang `IPipelineManager` và `IRuntimeParamManager`.
- **DEP-004**: `vms_engine_control` được thêm vào [infrastructure/CMakeLists.txt](/home/vms/Lantana/Dev/vms-engine/infrastructure/CMakeLists.txt) để HTTP server và broker consumer cùng link tới chung một handler.

## 5. Files

- **FILE-001**: [core/include/engine/core/config/config_types.hpp](/home/vms/Lantana/Dev/vms-engine/core/include/engine/core/config/config_types.hpp)
- **FILE-002**: [domain/src/runtime_param_rules.cpp](/home/vms/Lantana/Dev/vms-engine/domain/src/runtime_param_rules.cpp)
- **FILE-003**: [infrastructure/config_parser/include/engine/infrastructure/config_parser/yaml_config_parser.hpp](/home/vms/Lantana/Dev/vms-engine/infrastructure/config_parser/include/engine/infrastructure/config_parser/yaml_config_parser.hpp)
- **FILE-004**: [infrastructure/config_parser/src/yaml_config_parser.cpp](/home/vms/Lantana/Dev/vms-engine/infrastructure/config_parser/src/yaml_config_parser.cpp)
- **FILE-005**: [infrastructure/config_parser/src/yaml_parser_control_api.cpp](/home/vms/Lantana/Dev/vms-engine/infrastructure/config_parser/src/yaml_parser_control_api.cpp)
- **FILE-006**: [pipeline/include/engine/pipeline/pipeline_manager.hpp](/home/vms/Lantana/Dev/vms-engine/pipeline/include/engine/pipeline/pipeline_manager.hpp)
- **FILE-007**: [pipeline/src/pipeline_manager.cpp](/home/vms/Lantana/Dev/vms-engine/pipeline/src/pipeline_manager.cpp)
- **FILE-008**: [infrastructure/rest_api/include/engine/infrastructure/rest_api/pistache_server.hpp](/home/vms/Lantana/Dev/vms-engine/infrastructure/rest_api/include/engine/infrastructure/rest_api/pistache_server.hpp)
- **FILE-009**: [infrastructure/rest_api/src/pistache_server.cpp](/home/vms/Lantana/Dev/vms-engine/infrastructure/rest_api/src/pistache_server.cpp)
- **FILE-010**: [infrastructure/CMakeLists.txt](/home/vms/Lantana/Dev/vms-engine/infrastructure/CMakeLists.txt)
- **FILE-011**: [app/main.cpp](/home/vms/Lantana/Dev/vms-engine/app/main.cpp)
- **FILE-012**: [dev/configs/deepstream_default.yml](/home/vms/Lantana/Dev/vms-engine/dev/configs/deepstream_default.yml)
- **FILE-013**: [docs/configs/deepstream_default.yml](/home/vms/Lantana/Dev/vms-engine/docs/configs/deepstream_default.yml)
- **FILE-014**: [docs/architecture/deepstream/11_runtime_element_control.md](/home/vms/Lantana/Dev/vms-engine/docs/architecture/deepstream/11_runtime_element_control.md)
- **FILE-015**: [infrastructure/config_parser/src/yaml_parser_control_messaging.cpp](/home/vms/Lantana/Dev/vms-engine/infrastructure/config_parser/src/yaml_parser_control_messaging.cpp)
- **FILE-016**: [infrastructure/control/include/engine/infrastructure/control/runtime_control_handler.hpp](/home/vms/Lantana/Dev/vms-engine/infrastructure/control/include/engine/infrastructure/control/runtime_control_handler.hpp)
- **FILE-017**: [infrastructure/control/src/runtime_control_handler.cpp](/home/vms/Lantana/Dev/vms-engine/infrastructure/control/src/runtime_control_handler.cpp)
- **FILE-018**: [infrastructure/control/include/engine/infrastructure/control/runtime_control_message_consumer.hpp](/home/vms/Lantana/Dev/vms-engine/infrastructure/control/include/engine/infrastructure/control/runtime_control_message_consumer.hpp)
- **FILE-019**: [infrastructure/control/src/runtime_control_message_consumer.cpp](/home/vms/Lantana/Dev/vms-engine/infrastructure/control/src/runtime_control_message_consumer.cpp)

## 6. Testing

- **TEST-001**: Diagnostics check cho các file đổi bằng `get_errors` phải không có lỗi compile-level.
- **TEST-002**: Full build qua CMake Tools phải thành công sau khi thêm rest_api implementation.
- **TEST-003**: `GET /health` phải trả `status=ok` và `pipeline_id` đúng config.
- **TEST-004**: `GET /api/v1/pipelines/{pipeline_id}/state` phải trả state hiện tại của pipeline.
- **TEST-005**: `GET /api/v1/pipelines/{pipeline_id}/elements/osd/properties/display_bbox` phải trả giá trị hiện tại.
- **TEST-006**: `PATCH /api/v1/pipelines/{pipeline_id}/elements/osd/properties` với body `{"properties":{"display_bbox":false,"display_text":false}}` phải áp dụng thành công.
- **TEST-007**: Request đổi property ngoài allowlist phải trả `403 property_not_allowed`.
- **TEST-008**: Publish broker command `{"type":"get_pipeline_state","pipeline_id":"de1","request_id":"..."}` lên `control_messaging.channel` phải sinh reply có `status_code=200`.
- **TEST-009**: Publish broker command `{"type":"set_element_properties","pipeline_id":"de1","element_id":"osd","properties":{"display_bbox":false},"request_id":"...","reply_to":"..."}` phải áp dụng thành công và publish reply tương ứng.

## 7. Risks & Assumptions

- **RISK-001**: Full repo build chưa được chạy trong turn này vì build tool bị skip, nên link/runtime issues vẫn còn khả năng tồn tại.
- **RISK-002**: HTTP parser hiện chỉ support request có `Content-Length`, không support chunked transfer encoding.
- **RISK-003**: Allowlist hiện mới cover `osd.display_bbox` và `osd.display_text`; muốn mở rộng cho element khác phải bổ sung rule tương ứng.
- **RISK-004**: Broker reply hiện phụ thuộc vào việc `message_producer` được cấu hình và `reply_to` hoặc `control_messaging.reply_channel` có giá trị.
- **ASSUMPTION-001**: Runtime API chủ yếu được gọi trong lúc pipeline đã start, nên `GMainLoop` đang hoạt động cho main-context marshaling.
- **ASSUMPTION-002**: Sample source element id `sources` đủ ổn định cho smart_record và các runtime-control use case hiện tại.

## 8. Related Specifications / Further Reading

[docs/architecture/deepstream/11_runtime_element_control.md](/home/vms/Lantana/Dev/vms-engine/docs/architecture/deepstream/11_runtime_element_control.md)
[docs/architecture/deepstream/10_rest_api.md](/home/vms/Lantana/Dev/vms-engine/docs/architecture/deepstream/10_rest_api.md)
[docs/architecture/runtime_components/smart_record_probe_handler.md](/home/vms/Lantana/Dev/vms-engine/docs/architecture/runtime_components/smart_record_probe_handler.md)
