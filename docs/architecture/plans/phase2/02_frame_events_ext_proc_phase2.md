---
goal: "Phase 2 DeepStream - giản lược ext_proc cho frame_events theo live-surface path"
version: 2.0
date_created: 2026-03-10
last_updated: 2026-03-10
owner: VMS Engine Team
status: "In progress"
tags:
  [
    architecture,
    deepstream,
    frame_events,
    ext_proc,
    http,
    probes,
    runtime_components,
  ]
---

# Introduction

![Status: In progress](https://img.shields.io/badge/status-In_progress-yellow)

Kế hoạch này thay thế phương án `frame_events` ext-proc dựa trên `FrameEvidenceCache`, queue bounded, và worker pool bằng một phương án đơn giản hơn: sau khi `FrameEventsProbeHandler` publish `frame_events`, handler duyệt toàn bộ object vừa emit, đối chiếu `object_type` với `frame_events.ext_processor.rules[]`, encode crop ngay trên live `NvBufSurface`, rồi đẩy HTTP call sang detached thread giống mô hình của `ExternalProcessorService` legacy. Mục tiêu là giữ semantic path dễ hiểu, tránh duplicate cơ chế cache/materialization, và tận dụng luôn cadence đã có của `frame_events` như `heartbeat_interval_ms` và `min_emit_gap_ms`.

## 1. Requirements & Constraints

- **VER-001**: Đã xác minh `FrameEventsProbeHandler::on_buffer(...)` hiện publish `frame_events` sau khi đã quyết định emit reason và vẫn còn giữ `NvBufSurface* batch_surface` mapped trong probe callback.
- **VER-002**: Đã xác minh `FrameEventsExtProcService` hiện tại đang dùng `FrameEvidenceCache`, `FrameImageMaterializer`, queue bounded, worker threads, và throttle map riêng.
- **VER-003**: Đã xác minh `ExternalProcessorService` legacy trong `pipeline/extproc/ext_proc_svc.*` encode crop ngay trên live `NvBufSurface`, rồi dùng detached thread cho HTTP request.
- **REQ-001**: Giữ nguyên namespace config `event_handlers[].frame_events.ext_processor`. Không chuyển về root-level `ext_processor` của `crop_objects`.
- **REQ-002**: Bỏ dependency của `frame_events` ext-proc vào `FrameEvidenceCache`. Service mới không được resolve frame từ cache và không được phụ thuộc `EvidenceRequestService` hoặc `FrameImageMaterializer`.
- **REQ-003**: `FrameEventsProbeHandler` phải publish `frame_events` trước, rồi mới gọi ext-proc cho các object vừa emit. Không được gọi HTTP trước semantic publish.
- **REQ-004**: `FrameEventsProbeHandler` phải duyệt toàn bộ object đã emit; nếu `object.object_type` khớp `rules[].label` thì gọi ext-proc cho object đó. Không cần queue nội bộ, không cần job struct tách rời runtime metadata.
- **REQ-005**: `FrameEventsExtProcService` phải encode crop ngay trong probe path khi `NvBufSurface` còn sống, giống mô hình `ExternalProcessorService` legacy.
- **REQ-006**: HTTP call vẫn phải bất đồng bộ bằng detached thread để probe thread không chờ network round-trip.
- **REQ-007**: Bỏ throttle riêng của `FrameEventsExtProcService`. Cadence của `frame_events` do `heartbeat_interval_ms`, `min_emit_gap_ms`, và emit policy hiện có quyết định.
- **REQ-008**: Bỏ queue/worker lifecycle riêng của `FrameEventsExtProcService`. `start()`, `stop()`, và `is_running()` có thể giữ ở mức lightweight/no-op hoặc được giản lược, nhưng không còn spawn worker pool.
- **REQ-009**: Bỏ các field config không còn giá trị runtime cho nhánh này: `min_interval_sec`, `queue_capacity`, `worker_threads`, `crop_ref_preferred`.
- **REQ-010**: `FrameEventsExtProcConfig` sau refactor chỉ giữ các field cần thiết: `enable`, `publish_channel`, `jpeg_quality`, `connect_timeout_ms`, `request_timeout_ms`, `emit_empty_result`, `include_overview_ref`, `rules`.
- **REQ-011**: `FrameEventsExtProcRule` sau refactor chỉ giữ các field cần thiết: `label`, `endpoint`, `result_path`, `display_path`, `params`.
- **REQ-012**: Payload publish vẫn dùng event name `ext_proc` và phải giữ tương thích downstream ở các field cốt lõi: `event`, `pid`, `sid`, `sname`, `oid`, `object_key`, `class`, `class_id`, `conf`, `labels`, `result`, `display`, `event_ts`.
- **REQ-013**: Payload publish phải tiếp tục mang correlation fields mới của `frame_events`: `pipeline_id`, `source_id`, `source_name`, `frame_key`, `frame_ts_ms`, `instance_key`, `tracker_id`, `crop_ref`, và tùy chọn `overview_ref`.
- **REQ-014**: Nếu external API trả JSON parse fail hoặc `result_path` rỗng, hành vi phải tuân theo `emit_empty_result`. Khi `false` thì bỏ qua publish; khi `true` thì publish với `status = "empty_result"`.
- **REQ-015**: `FrameEventsProbeHandler` vẫn được phép cache emitted frame cho evidence workflow nếu `evidence.enable = true`; tuy nhiên ext-proc path không được phụ thuộc vào việc cache thành công hay thất bại.
- **REQ-016**: `FrameEventsExtProcService` phải là handler-owned service. Mỗi `FrameEventsProbeHandler` tự tạo instance riêng trong `configure(...)`, giống pattern `CropObjectHandler -> ExternalProcessorService`, và không còn lifecycle ownership ở `PipelineManager`.
- **REQ-017**: Không thay đổi `ExternalProcessorService` legacy cho `crop_objects`, trừ khi tái sử dụng có kiểm soát các helper HTTP/JSON nội bộ mà không phá contract cũ.
- **SEC-001**: Không log raw JPEG bytes, multipart body, secret query parameters, hoặc credentials embedded trong endpoint.
- **SEC-002**: Không publish absolute local file paths hoặc binary payload trong message `ext_proc`.
- **CON-001**: C++17 only. Không dùng C++20/23.
- **CON-002**: Nhánh `pipeline/` chỉ phụ thuộc `core/` và DeepStream/GStreamer như hiện tại; không kéo dependency mới từ `infrastructure/`.
- **PAT-001**: `frame_events` ext-proc phải là fire-and-forget sidecar sau semantic publish, không phải workflow request/reply.
- **PAT-002**: Live-surface encode chỉ được thực hiện khi probe callback còn giữ `NvBufSurface` mapped. Detached thread chỉ nhận JPEG bytes đã tách khỏi metadata.

## 2. Implementation Steps

### Implementation Phase 1

- **GOAL-001**: Cập nhật plan, config model, parser, và docs để phản ánh thiết kế giản lược theo live-surface path.

| Task     | Description                                                                                                                                                                                                                                                                                                     | Completed | Date       |
| -------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------- | ---------- |
| TASK-001 | Cập nhật `/home/vms/Lantana/Dev/vms-engine/docs/architecture/plans/phase2/02_frame_events_ext_proc_phase2.md` để loại bỏ mọi giả định về queue, worker pool, cache-driven materialization, và thay bằng flow `publish frame_events -> iterate emitted objects -> encode crop on live surface -> detached HTTP`. | ✅        | 2026-03-10 |
| TASK-002 | Cập nhật `/home/vms/Lantana/Dev/vms-engine/core/include/engine/core/config/config_types.hpp` để giản lược `FrameEventsExtProcConfig` và `FrameEventsExtProcRule`, xóa các field `min_interval_sec`, `queue_capacity`, `worker_threads`, `crop_ref_preferred`.                                                   |           |            |
| TASK-003 | Cập nhật `/home/vms/Lantana/Dev/vms-engine/infrastructure/config_parser/src/yaml_parser_handlers.cpp` để parser chỉ đọc các field còn lại của `frame_events.ext_processor` và bỏ parse các field đã bị loại.                                                                                                    |           |            |
| TASK-004 | Cập nhật `/home/vms/Lantana/Dev/vms-engine/docs/configs/deepstream_default.yml` để ví dụ `frame_events.ext_processor` không còn `min_interval_sec`, `queue_capacity`, `worker_threads`, và bám đúng endpoint/params hiện tại.                                                                                   |           |            |
| TASK-005 | Cập nhật runtime docs trong `/home/vms/Lantana/Dev/vms-engine/docs/architecture/runtime_components/frame_events_ext_proc_service.md` và `/home/vms/Lantana/Dev/vms-engine/docs/architecture/runtime_components/ext_proc_svc.md` để mô tả thiết kế live-surface mới.                                             |           |            |

### Implementation Phase 2

- **GOAL-002**: Refactor code từ cache/queue model sang direct live-surface ext-proc giống `ext_proc_svc.cpp` nhưng vẫn giữ payload và config riêng cho `frame_events`.

<!-- markdownlint-disable MD060 -->

| Task     | Description                                                                                                                                                                                                                                                                                                                                                                                                           | Completed | Date |
| -------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------- | ---- |
| TASK-006 | Cập nhật `/home/vms/Lantana/Dev/vms-engine/pipeline/include/engine/pipeline/extproc/frame_events_ext_proc_service.hpp` để bỏ `FrameEventsExtProcJob`, queue state, worker state, cache dependency, và thêm API trực tiếp kiểu `process_object(...)` nhận live `NvDsObjectMeta*`, `NvDsFrameMeta*`, `NvBufSurface*`, cùng metadata semantic cần publish.                                                               |           |      |
| TASK-007 | Cập nhật `/home/vms/Lantana/Dev/vms-engine/pipeline/src/extproc/frame_events_ext_proc_service.cpp` để encode crop bytes ngay trong probe path, lookup rule theo `object_type`, rồi spawn detached HTTP thread parse JSON và publish `ext_proc`. Không còn `enqueue()`, `worker_loop()`, `process_job()`, hay throttle map.                                                                                            |           |      |
| TASK-008 | Cập nhật `/home/vms/Lantana/Dev/vms-engine/pipeline/include/engine/pipeline/probes/frame_events_probe_handler.hpp` và `/home/vms/Lantana/Dev/vms-engine/pipeline/src/probes/frame_events_probe_handler.cpp` để handler tự tạo `FrameEventsExtProcService` trong `configure(...)`, rồi sau `publish_frame_message(...)` duyệt lại object metadata tương ứng và gọi `process_object(...)` cho mọi object có rule match. |           |      |
| TASK-009 | Cập nhật `/home/vms/Lantana/Dev/vms-engine/pipeline/include/engine/pipeline/probes/probe_handler_manager.hpp` và `/home/vms/Lantana/Dev/vms-engine/pipeline/src/probes/probe_handler_manager.cpp` để không còn truyền service pointer từ `PipelineManager` vào `FrameEventsProbeHandler`.                                                                                                                             |           |      |
| TASK-010 | Cập nhật `/home/vms/Lantana/Dev/vms-engine/pipeline/include/engine/pipeline/pipeline_manager.hpp` và `/home/vms/Lantana/Dev/vms-engine/pipeline/src/pipeline_manager.cpp` để bỏ ownership/start/stop của `FrameEventsExtProcService` ở mức pipeline.                                                                                                                                                                  |           |      |
| TASK-011 | Gỡ bỏ runtime dependency của `frame_events` ext-proc vào `/home/vms/Lantana/Dev/vms-engine/pipeline/src/evidence/frame_image_materializer.cpp` và `/home/vms/Lantana/Dev/vms-engine/pipeline/include/engine/pipeline/evidence/frame_image_materializer.hpp`; evidence helper vẫn giữ nguyên cho evidence workflow.                                                                                                    |           |      |

<!-- markdownlint-enable MD060 -->

### Implementation Phase 3

- **GOAL-003**: Đồng bộ docs và xác minh build sau refactor.

| Task     | Description                                                                                                                                                                                                                                                               | Completed | Date |
| -------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------- | ---- |
| TASK-012 | Cập nhật `/home/vms/Lantana/Dev/vms-engine/docs/architecture/runtime_components/frame_events_ext_proc_service.md` để mô tả object filtering theo `rule.label`, live-surface encode, detached HTTP thread, và việc bỏ queue/cache/throttle riêng.                          |           |      |
| TASK-013 | Cập nhật `/home/vms/Lantana/Dev/vms-engine/docs/architecture/runtime_components/frame_events_probe_handler.md` để ghi rõ thứ tự mới: `collect -> decide emit -> build refs -> optional cache for evidence -> publish frame_events -> call ext_proc for matching objects`. |           |      |
| TASK-014 | Chạy build/diagnostics cho các file C++ đã chỉnh để xác minh không còn dependency compile-time vào queue/cache APIs cũ của `FrameEventsExtProcService`.                                                                                                                   |           |      |

## 3. Alternatives

- **ALT-001**: Giữ nguyên queue bounded + worker pool + cache resolve. Không chọn vì phức tạp quá mức cho use case hiện tại và duplicate cadence/throttle logic đã có ở `frame_events`.
- **ALT-002**: Gọi HTTP đồng bộ ngay trong `FrameEventsProbeHandler`. Không chọn vì sẽ block pad probe bởi network latency.
- **ALT-003**: Tạo một service dùng chung ở `PipelineManager` cho mọi handler `frame_events`. Không chọn vì ownership đó không còn cần thiết sau khi ext-proc path đã trở thành live-surface, probe-local flow giống `crop_objects`.
- **ALT-004**: Dùng `FrameEvidenceCache` chỉ để encode crop cho ext-proc nhưng bỏ worker queue. Không chọn vì vẫn giữ coupling không cần thiết với evidence path trong khi live `NvBufSurface` đã sẵn có ở đúng nơi cần gọi ext-proc.

## 4. Dependencies

- **DEP-001**: `FrameEventsProbeHandler::on_buffer(...)` phải tiếp tục giữ `NvBufSurface` mapped đủ lâu để service encode crop trước khi probe return.
- **DEP-002**: `IMessageProducer` hiện có phải tiếp tục publish được `ext_proc` payload trên channel riêng của `frame_events.ext_processor.publish_channel`.
- **DEP-003**: libcurl và nlohmann/json tiếp tục là dependency runtime cho HTTP request và response parsing.

## 5. Files

- **FILE-001**: `/home/vms/Lantana/Dev/vms-engine/docs/architecture/plans/phase2/02_frame_events_ext_proc_phase2.md`
- **FILE-002**: `/home/vms/Lantana/Dev/vms-engine/core/include/engine/core/config/config_types.hpp`
- **FILE-003**: `/home/vms/Lantana/Dev/vms-engine/infrastructure/config_parser/src/yaml_parser_handlers.cpp`
- **FILE-004**: `/home/vms/Lantana/Dev/vms-engine/pipeline/include/engine/pipeline/extproc/frame_events_ext_proc_service.hpp`
- **FILE-005**: `/home/vms/Lantana/Dev/vms-engine/pipeline/src/extproc/frame_events_ext_proc_service.cpp`
- **FILE-006**: `/home/vms/Lantana/Dev/vms-engine/pipeline/include/engine/pipeline/probes/frame_events_probe_handler.hpp`
- **FILE-007**: `/home/vms/Lantana/Dev/vms-engine/pipeline/src/probes/frame_events_probe_handler.cpp`
- **FILE-008**: `/home/vms/Lantana/Dev/vms-engine/pipeline/include/engine/pipeline/probes/probe_handler_manager.hpp`
- **FILE-009**: `/home/vms/Lantana/Dev/vms-engine/pipeline/src/probes/probe_handler_manager.cpp`
- **FILE-010**: `/home/vms/Lantana/Dev/vms-engine/pipeline/include/engine/pipeline/pipeline_manager.hpp`
- **FILE-011**: `/home/vms/Lantana/Dev/vms-engine/pipeline/src/pipeline_manager.cpp`
- **FILE-012**: `/home/vms/Lantana/Dev/vms-engine/docs/configs/deepstream_default.yml`
- **FILE-013**: `/home/vms/Lantana/Dev/vms-engine/docs/architecture/runtime_components/frame_events_ext_proc_service.md`
- **FILE-014**: `/home/vms/Lantana/Dev/vms-engine/docs/architecture/runtime_components/frame_events_probe_handler.md`
- **FILE-015**: `/home/vms/Lantana/Dev/vms-engine/docs/architecture/runtime_components/ext_proc_svc.md`

## 6. Testing

- **TEST-001**: Build validation cho `core/`, `pipeline/`, `infrastructure/config_parser/`, và executable chính sau khi bỏ queue/cache APIs cũ.
- **TEST-002**: Semantic ordering check: xác minh `frame_events` vẫn publish trước, ext-proc chỉ chạy sau publish call trong cùng buffer callback.
- **TEST-003**: Rule filtering check: với nhiều object trên frame, chỉ object có `object_type` khớp `rules[].label` mới sinh HTTP request.
- **TEST-004**: Legacy safety: bật `crop_objects.ext_processor` và `frame_events.ext_processor` cùng lúc, xác minh mỗi probe tự sở hữu service riêng và mỗi nhánh vẫn publish đúng schema/channel riêng của nó.
- **TEST-005**: Empty-result policy: mock HTTP response không có `result_path` và xác minh `emit_empty_result` hoạt động đúng.

## 7. Risks & Assumptions

- **RISK-001**: Bỏ worker queue đồng nghĩa số detached thread sẽ tỉ lệ với số object khớp rule trên mỗi frame_events emit. Với current expectation, cadence của `frame_events` được coi là đủ thấp để chấp nhận trade-off này.
- **RISK-002**: Live-surface encode trong probe path thêm chi phí GPU encode vào callback; nếu số object/rule tăng mạnh thì cần đo lại latency.
- **RISK-003**: Nếu sau này downstream muốn ext-proc chạy độc lập với live surface, thiết kế này sẽ cần quay lại cache/materialization path.
- **ASSUMPTION-001**: `frame_events` hiện đã có emit policy đủ chặt để thay cho throttle riêng của ext-proc.
- **ASSUMPTION-002**: Use case trước mắt chỉ cần fire-and-forget enrichment, không cần retry queue, worker metrics, hay backpressure control.

## 8. Related Specifications / Further Reading

- `/home/vms/Lantana/Dev/vms-engine/docs/architecture/runtime_components/frame_events_probe_handler.md`
- `/home/vms/Lantana/Dev/vms-engine/docs/architecture/runtime_components/frame_events_ext_proc_service.md`
- `/home/vms/Lantana/Dev/vms-engine/docs/architecture/runtime_components/ext_proc_svc.md`
- `/home/vms/Lantana/Dev/vms-engine/pipeline/src/probes/frame_events_probe_handler.cpp`
- `/home/vms/Lantana/Dev/vms-engine/pipeline/src/extproc/frame_events_ext_proc_service.cpp`
