---
goal: "Phase 2 DeepStream - ext_proc_svc bất đồng bộ cho frame_events_probe_handler"
version: 1.0
date_created: 2026-03-10
last_updated: 2026-03-10
owner: VMS Engine Team
status: "Planned"
tags: [architecture, deepstream, frame_events, ext_proc, http, enrichment, probes, evidence]
---

# Introduction

![Status: Planned](https://img.shields.io/badge/status-Planned-blue)

Kế hoạch này bổ sung phase 2 bằng một nhánh `ext_proc_svc` mới dành riêng cho `FrameEventsProbeHandler`, tách hẳn khỏi `ExternalProcessorService` legacy đang phục vụ `CropObjectHandler`. Mục tiêu là giữ nguyên nguyên tắc `semantic first, evidence later` của `frame_events`, nhưng vẫn cho phép engine tự động gọi external AI service như face recognition, license plate lookup, OCR, hoặc enrichment HTTP khác trên đúng frame đã emit mà không kéo `curl`, `nvds_obj_enc_finish()`, hoặc live `NvBufSurface` dependency quay trở lại pad probe nóng.

Kiến trúc đích của plan này là: `FrameEventsProbeHandler` tiếp tục publish `frame_events` nhanh và cache emitted frame theo `frame_key`; một service mới chạy worker-scoped sẽ nhận job ext-proc nội bộ cho các object phù hợp rule, resolve frame snapshot từ `FrameEvidenceCache`, materialize JPEG crop in-memory, gọi HTTP endpoint, parse JSON response, rồi publish `ext_proc` event ra channel riêng. Flow này phải giữ được ba tính chất đồng thời: không block semantic path, không phá vỡ contract ext-proc cũ của `crop_objects`, và không làm mơ hồ routing khi một worker phục vụ nhiều pipeline.

Plan này không thay thế `docs/architecture/plans/phase2/01_deepstream_phase2.md`; nó là plan bổ sung cho nhánh enrichment bất đồng bộ sau khi `frame_events`, `FrameEvidenceCache`, và `EvidenceRequestService` đã trở thành nền tảng chính của phase 2. `ExternalProcessorService` hiện tại vẫn tồn tại nguyên vẹn để phục vụ `CropObjectHandler` legacy. Mọi thay đổi trong file này đều hướng tới một service mới, config mới, và lifecycle mới bám vào `frame_events` thay vì tái sử dụng trực tiếp đường code live-surface của `crop_objects`.

## 1. Requirements & Constraints

- **VER-001**: Đã xác minh `ExternalProcessorService` hiện tại trong `pipeline/include/engine/pipeline/probes/ext_proc_svc.hpp` và `pipeline/src/probes/ext_proc_svc.cpp` phụ thuộc trực tiếp vào `NvDsObjectMeta*`, `NvDsFrameMeta*`, và `NvBufSurface*` còn đang mapped trong pad probe callback.
- **VER-002**: Đã xác minh `ExternalProcessorService::process_object(...)` hiện encode JPEG in-memory ngay tại probe call site rồi mới `std::thread::detach()` cho HTTP call, nghĩa là service này gắn chặt với `CropObjectHandler` và live surface lifecycle.
- **VER-003**: Đã xác minh `CropObjectHandler` hiện là nơi duy nhất instantiate `ExternalProcessorService` trong `pipeline/src/probes/crop_object_handler.cpp`, còn `FrameEventsProbeHandler` hiện không có dependency nào tới ext proc service.
- **VER-004**: Đã xác minh `YamlConfigParser::parse_handlers(...)` hiện parse `ext_processor` ở cấp root của `event_handlers[]` vào `EventHandlerConfig::ext_processor`, không có namespace riêng cho nhánh `frame_events`.
- **VER-005**: Đã xác minh `FrameEventsProbeHandler::on_buffer(...)` hiện đã có `FrameEvidenceCache` handoff, deterministic `overview_ref` / `crop_ref`, và publish semantics độc lập với encode path.
- **VER-006**: Đã xác minh `ProbeHandlerManager::attach_probes(...)` hiện attach `frame_events` qua `FrameEventsProbeHandler` và `crop_objects` qua `CropObjectHandler` theo hai nhánh runtime riêng.
- **REQ-001**: Tạo một service mới phục vụ `frame_events`, không tái sử dụng trực tiếp class `ExternalProcessorService` hiện tại cho nhánh này. Class mới phải có lifecycle riêng, queue riêng, và contract riêng nhưng có thể tái sử dụng helper HTTP/JSON encode ở mức implementation nếu an toàn.
- **REQ-002**: Giữ nguyên `ExternalProcessorService` hiện có cho `CropObjectHandler`. Không đổi signature `ExternalProcessorService::configure(...)`, không đổi `ExternalProcessorService::process_object(...)`, không đổi schema legacy đang publish từ nhánh `crop_objects`.
- **REQ-003**: Ext-proc cho `frame_events` phải chạy bất đồng bộ hoàn toàn so với semantic publish path. `FrameEventsProbeHandler::publish_frame_message(...)` không được chờ HTTP, không được chờ JPEG encode cho ext proc, và không được block bởi worker queue ngoài một thao tác enqueue bounded, fail-fast.
- **REQ-004**: Nhánh `frame_events` phải có config ext-proc riêng, không tái sử dụng root-level `handler.ext_processor` vì block đó hiện đang mang semantics legacy của `crop_objects`. Config mới phải được namescope dưới `frame_events:` để tránh mơ hồ khi cùng một YAML file bật đồng thời `crop_objects` và `frame_events`.
- **REQ-005**: Thêm `FrameEventsExtProcRule` và `FrameEventsExtProcConfig` mới trong `core/include/engine/core/config/config_types.hpp`. `FrameEventsConfig` phải mang `std::optional<FrameEventsExtProcConfig> ext_processor;`.
- **REQ-006**: `FrameEventsExtProcConfig` phải có tối thiểu các field sau với default tường minh: `enable = false`, `publish_channel = ""`, `min_interval_sec = 5`, `queue_capacity = 256`, `worker_threads = 2`, `jpeg_quality = 80`, `connect_timeout_ms = 5000`, `request_timeout_ms = 10000`, `emit_empty_result = false`, `include_overview_ref = true`, `rules = []`.
- **REQ-007**: Mỗi `FrameEventsExtProcRule` phải có tối thiểu: `label`, `endpoint`, `result_path`, `display_path`, `params`, và optional `crop_ref_preferred = true`. Rule lookup phải key theo `object_type` hoặc label classifier mà plan này chỉ định rõ trong implementation doc.
- **REQ-008**: Service mới phải dùng `FrameEvidenceCache` hoặc helper materialization trên snapshot owned bởi cache để cắt crop in-memory. Không được giữ borrowed `GstBuffer*`, `NvBufSurface*`, `NvDsFrameMeta*`, hay `NvDsObjectMeta*` sau khi `FrameEventsProbeHandler::on_buffer(...)` kết thúc.
- **REQ-009**: Throttle key cho nhánh mới phải xác định theo `pipeline_id:source_id:object_id:label` hoặc equivalent deterministic tuple có cùng ý nghĩa. Throttle phải worker-scoped và monotonic-time based.
- **REQ-010**: Service mới phải dùng bounded worker pool thay vì `std::thread::detach()` một thread cho mỗi request như nhánh legacy. Mục tiêu là tránh unbounded thread growth khi một frame có nhiều object hoặc external API chậm.
- **REQ-011**: Nếu queue ext-proc đầy, engine phải drop job ext-proc mới với log warning có rate-limit, nhưng tuyệt đối không làm fail `frame_events` publish.
- **REQ-012**: Kênh publish của ext-proc mới phải tách khỏi `frame_events.channel`. `publish_channel` là field bắt buộc khi `ext_processor.enable = true`. Không được trộn `frame_events` schema và `ext_proc` schema trên cùng stream/topic.
- **REQ-013**: Payload publish từ nhánh mới phải giữ tương thích downstream ở mức field cốt lõi của `ext_proc` legacy: `event`, `pid`, `sid`, `sname`, `oid`, `object_key`, `class`, `class_id`, `conf`, `labels`, `result`, `display`, `event_ts`. Đồng thời phải bổ sung routing metadata mới: `schema_version`, `frame_key`, `frame_ts_ms`, `instance_key`, và nếu có thì `overview_ref`, `crop_ref`.
- **REQ-014**: Event name mặc định vẫn là `ext_proc` để không buộc downstream đổi event discriminator, nhưng schema mới phải được version hóa bằng `schema_version` và documented rõ là ext-proc emitted from `frame_events` path.
- **REQ-015**: Nếu external API trả JSON parse fail hoặc không tìm thấy `result_path`, service phải tuân theo `emit_empty_result`. Khi `emit_empty_result = false`, job chỉ log và bỏ qua publish. Khi `emit_empty_result = true`, vẫn publish `ext_proc` với `result = ""`, `display = ""`, và `status = "empty_result"` hoặc equivalent field được mô tả rõ trong docs.
- **REQ-016**: Nhánh mới phải publish correlation đủ để downstream patch event/timeline hoặc debug chéo với `frame_events`: tối thiểu gồm `pipeline_id`, `source_id`, `source_name`, `frame_key`, `frame_ts_ms`, `object_key`, `instance_key`, `tracker_id`.
- **REQ-017**: `FrameEventsProbeHandler` chỉ được handoff job sau khi frame đã được cache thành công hoặc đã xác định rõ chiến lược fallback. Plan này chọn ưu tiên cache-first: nếu frame không cache được thì ext-proc job của frame đó phải bị skip để tránh mismatch.
- **REQ-018**: Ext-proc mới phải là sidecar tùy chọn của `frame_events`, không phải một phần bắt buộc của semantic contract. Nếu config tắt hoặc service lỗi, `frame_events` vẫn hoạt động đầy đủ.
- **REQ-019**: Tài liệu phải chỉ rõ thứ tự thực thi chuẩn trong `FrameEventsProbeHandler::on_buffer(...)`: `collect objects -> decide emit -> build refs -> store cache -> publish frame_events -> enqueue ext_proc jobs`. Không được đổi thứ tự theo kiểu gọi ext proc trước semantic publish.
- **REQ-020**: Plan phải định nghĩa rõ API boundary giữa `FrameEventsProbeHandler` và service mới bằng job struct riêng, ví dụ `FrameEventsExtProcJob`, chứa toàn bộ dữ liệu cần thiết để worker xử lý mà không cần truy cập lại DeepStream metadata gốc.
- **REQ-021**: Nếu cần encode helper dùng lại logic từ evidence subsystem, helper đó phải nằm trong vùng dùng chung mới ở `pipeline/evidence` hoặc `pipeline/extproc`, không nhúng logic cắt crop từ cache sâu vào `FrameEventsProbeHandler`.
- **REQ-022**: Startup log phải hiển thị rõ khi `frame_events.ext_processor.enable = true`, gồm `publish_channel`, `worker_threads`, `queue_capacity`, `min_interval_sec`, `jpeg_quality`, và số rule đang bật.
- **REQ-023**: YAML mẫu và docs phải minh họa ít nhất 2 use case: `face` recognition và `license_plate` recognition chạy trên `frame_events` path.
- **REQ-024**: `frame_events` ext-proc path phải dùng cùng backend broker với `messaging.type`. Nếu producer đang là Redis thì publish sang Redis Stream; nếu đang là Kafka thì publish sang Kafka topic, thông qua `IMessageProducer` hiện có.
- **REQ-025**: Plan này không được mở rộng scope sang việc downstream synchronous-request ext proc hoặc tạo channel request/reply mới cho ext proc. Nhánh mới là engine-side fire-and-forget enrichment sau semantic emit.
- **SEC-001**: Không log raw endpoint credentials, query secret, JWT, API key, hoặc multipart body của ảnh crop.
- **SEC-002**: Không publish absolute local file path hoặc binary payload trong `ext_proc` message. Chỉ publish refs tương đối và semantic metadata.
- **SEC-003**: HTTP timeout và retry policy phải bounded; không được để worker thread treo vô hạn do external service chậm.
- **CON-001**: Chỉ dùng C++17, DeepStream 8.0, libcurl, nlohmann/json, và pattern interface-first hiện có của repo.
- **CON-002**: Không thay đổi namespace `engine::` và không kéo dependency từ `pipeline/` sang `infrastructure/` theo chiều ngược kiến trúc.
- **CON-003**: Không sửa contract evidence request/ready đã được chốt trong `FrameEventsProbeHandler` và `EvidenceRequestService`. Ext-proc mới phải đứng cạnh workflow đó, không thay thế nó.
- **PAT-001**: Tách dứt khoát `legacy crop_objects ext proc` và `frame_events ext proc` bằng config, class, queue, docs, và startup log riêng.
- **PAT-002**: `frame_events` ext-proc phải dùng `FrameEvidenceCache` làm source of truth cho image materialization, giống tinh thần `semantic first, evidence later` đã chốt ở phase 2.
- **PAT-003**: Failure ở ext-proc path không được làm mất semantic event. Semantic path luôn là ưu tiên cao hơn media/enrichment path.

## 2. Implementation Steps

<!-- markdownlint-disable MD060 -->

### Implementation Phase 1

- **GOAL-001**: Chốt xong config, schema, và boundary kỹ thuật cho nhánh `frame_events ext_proc` mà không làm nhập nhằng với `crop_objects` legacy.
- **COMPLETION-001**: `config_types.hpp`, YAML parser, config mẫu, và tài liệu đều mô tả nhất quán block `frame_events.ext_processor`, publish channel riêng, payload `ext_proc` version hóa, và job boundary mới.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-001 | Cập nhật `/home/vms/Lantana/Dev/vms-engine/core/include/engine/core/config/config_types.hpp` để thêm `struct FrameEventsExtProcRule` và `struct FrameEventsExtProcConfig`, sau đó nhúng `std::optional<FrameEventsExtProcConfig> ext_processor;` vào `FrameEventsConfig`. Các field phải đúng như `REQ-006` và có comment Doxygen mô tả rõ đây là config chỉ dành cho `trigger: frame_events`. |  |  |
| TASK-002 | Cập nhật `/home/vms/Lantana/Dev/vms-engine/infrastructure/config_parser/src/yaml_parser_handlers.cpp` để parse nested block `frame_events.ext_processor`. Parser phải không đọc block root-level `ext_processor` vào nhánh mới, và phải giữ nguyên parse path hiện tại cho `handler.ext_processor` legacy. |  |  |
| TASK-003 | Cập nhật `/home/vms/Lantana/Dev/vms-engine/docs/configs/deepstream_default.yml` để thêm ví dụ `frame_events:` có nested `ext_processor:` với `publish_channel`, `worker_threads`, `queue_capacity`, `min_interval_sec`, và hai rule mẫu cho `face` và `license_plate`. |  |  |
| TASK-004 | Cập nhật `/home/vms/Lantana/Dev/vms-engine/docs/architecture/probes/frame_events_probe_handler.md` để bổ sung section mới mô tả ext-proc sidecar cho `frame_events`, thứ tự `cache -> frame_events publish -> ext_proc enqueue`, và payload ext-proc version mới. |  |  |
| TASK-005 | Cập nhật `/home/vms/Lantana/Dev/vms-engine/docs/architecture/probes/ext_proc_svc.md` để tách rõ hai thế hệ service: `ExternalProcessorService` hiện tại cho `crop_objects` và service mới cho `frame_events`. Tài liệu phải ghi rõ service mới không còn chạy trên live mapped surface trong pad probe. |  |  |

### Implementation Phase 2

- **GOAL-002**: Implement service mới, job queue bounded, crop materialization từ cache, và publish path ext-proc tương thích downstream nhưng không block `frame_events`.
- **COMPLETION-002**: Engine có thể nhận object từ `FrameEventsProbeHandler`, enqueue job ext-proc, resolve snapshot từ cache, gọi HTTP endpoint, và publish `ext_proc` event ra channel riêng mà không ảnh hưởng semantic throughput.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-006 | Tạo file mới `/home/vms/Lantana/Dev/vms-engine/pipeline/include/engine/pipeline/extproc/frame_events_ext_proc_service.hpp` khai báo `FrameEventsExtProcService`, `FrameEventsExtProcJob`, `FrameEventsExtProcResult`, và public API tối thiểu gồm `configure(...)`, `start()`, `stop()`, `enqueue(...)`, `is_running()`. API phải nhận `IMessageProducer*`, `FrameEvidenceCache*`, và `FrameEventsExtProcConfig` mà không nhận borrowed DeepStream metadata pointer. |  |  |
| TASK-007 | Tạo file mới `/home/vms/Lantana/Dev/vms-engine/pipeline/src/extproc/frame_events_ext_proc_service.cpp` để implement bounded queue, worker thread pool, throttle map, libcurl multipart POST, JSON parse theo dot-path, và publish `ext_proc` JSON message. Worker pool phải dùng `std::condition_variable` + `std::deque` hoặc equivalent bounded queue, không dùng detached thread per request. |  |  |
| TASK-008 | Tạo helper materialization dùng chung trong `/home/vms/Lantana/Dev/vms-engine/pipeline/include/engine/pipeline/evidence/frame_image_materializer.hpp` và `/home/vms/Lantana/Dev/vms-engine/pipeline/src/evidence/frame_image_materializer.cpp` để cắt object crop từ cached frame snapshot thành JPEG bytes in-memory theo `jpeg_quality`. Helper này phải dùng được cho ext-proc mới mà không ghi file ra disk. |  |  |
| TASK-009 | Cập nhật `/home/vms/Lantana/Dev/vms-engine/pipeline/include/engine/pipeline/evidence/frame_evidence_cache.hpp` và `/home/vms/Lantana/Dev/vms-engine/pipeline/src/evidence/frame_evidence_cache.cpp` nếu cần để expose read-only resolve API phù hợp cho ext-proc worker, bao gồm lookup exact theo `frame_key` và truy xuất object snapshot theo `object_key` hoặc `instance_key`. Không thêm API nào trả borrowed GPU pointer ra ngoài cache. |  |  |
| TASK-010 | Cập nhật `/home/vms/Lantana/Dev/vms-engine/pipeline/include/engine/pipeline/probes/frame_events_probe_handler.hpp` và `/home/vms/Lantana/Dev/vms-engine/pipeline/src/probes/frame_events_probe_handler.cpp` để nhận thêm dependency `FrameEventsExtProcService*` hoặc equivalent dispatcher pointer trong `configure(...)`. `on_buffer(...)` phải enqueue job ext-proc sau khi `store_frame(...)` thành công và sau khi `publish_frame_message(...)` hoàn tất. |  |  |
| TASK-011 | Cập nhật `/home/vms/Lantana/Dev/vms-engine/pipeline/include/engine/pipeline/probes/probe_handler_manager.hpp` và `/home/vms/Lantana/Dev/vms-engine/pipeline/src/probes/probe_handler_manager.cpp` để wiring dependency của `FrameEventsExtProcService` vào riêng nhánh `frame_events`, giữ nguyên nhánh `crop_objects` hiện tại. |  |  |
| TASK-012 | Cập nhật `/home/vms/Lantana/Dev/vms-engine/pipeline/include/engine/pipeline/pipeline_manager.hpp`, `/home/vms/Lantana/Dev/vms-engine/pipeline/src/pipeline_manager.cpp`, và `/home/vms/Lantana/Dev/vms-engine/app/main.cpp` để tạo lifecycle cho `FrameEventsExtProcService`, start/stop cùng process, log cấu hình lúc startup, và bảo đảm service chỉ được bật khi `frame_events.ext_processor.enable = true`. |  |  |

### Implementation Phase 3

- **GOAL-003**: Hoàn thiện tài liệu, payload samples, và test coverage để nhánh `frame_events ext_proc` có thể được bật an toàn cùng với `evidence_request/evidence_ready` và `crop_objects` legacy.
- **COMPLETION-003**: Docs mô tả rõ boundary giữa ba luồng `frame_events`, `evidence`, `ext_proc`; test xác minh semantic path không bị block; payload ext-proc mới tương thích downstream và có correlation đầy đủ.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-013 | Thêm vào `/home/vms/Lantana/Dev/vms-engine/docs/architecture/probes/frame_events_probe_handler.md` ít nhất 2 ví dụ JSON cho ext-proc path: một case `result` thành công và một case `empty_result` hoặc `error`, đều có `frame_key`, `frame_ts_ms`, `object_key`, `instance_key`, `overview_ref`, `crop_ref`. |  |  |
| TASK-014 | Cập nhật `/home/vms/Lantana/Dev/vms-engine/docs/architecture/probes/ext_proc_svc.md` với sequence diagram mới cho `frame_events ext proc`: `FrameEventsProbeHandler -> FrameEvidenceCache -> FrameEventsExtProcService queue -> worker -> HTTP API -> broker publish`. |  |  |
| TASK-015 | Cập nhật `/home/vms/Lantana/Dev/vms-engine/docs/architecture/plans/phase2/01_deepstream_phase2.md` để thêm cross-reference sang plan này, ghi rõ ext-proc cho `frame_events` được tách ra thành kế hoạch bổ sung riêng, không còn bị gói chung trong vai trò sidecar của `crop_objects`. |  |  |

<!-- markdownlint-enable MD060 -->

## 3. Alternatives

- **ALT-001**: Dùng lại trực tiếp `ExternalProcessorService` hiện tại bằng cách gọi nó từ `FrameEventsProbeHandler::on_buffer(...)`. Không chọn vì service hiện tại yêu cầu live mapped `NvBufSurface` và detached thread per request, trái với boundary mới của `frame_events`.
- **ALT-002**: Nhét config ext-proc mới vào root-level `handler.ext_processor`. Không chọn vì block này đang thuộc semantics legacy của `crop_objects`, dễ làm YAML mơ hồ và wiring sai service.
- **ALT-003**: Gọi external API đồng bộ ngay trong pad probe rồi publish kết quả cùng `frame_events`. Không chọn vì làm semantic path bị block bởi HTTP latency và phá vỡ nguyên tắc `semantic first`.
- **ALT-004**: Tạo request/reply channel mới cho ext-proc tương tự `evidence_request/evidence_ready`. Không chọn vì use case ext-proc ở đây là engine-side fire-and-forget enrichment, không phải workflow downstream-driven materialization.
- **ALT-005**: Ghi crop JPEG ra disk rồi mới gửi file path sang external service. Không chọn vì tăng disk I/O, tạo lifecycle cleanup mới, và không cần thiết khi external API chỉ cần bytes in-memory.
- **ALT-006**: Tạo một thread detached cho mỗi object như nhánh legacy. Không chọn vì rủi ro unbounded concurrency dưới tải cao và khó kiểm soát shutdown.

## 4. Dependencies

- **DEP-001**: `FrameEvidenceCache` và emitted-frame snapshot workflow từ phase 2 phải hoạt động ổn định trước khi nhánh ext-proc mới có thể resolve image đúng theo `frame_key`.
- **DEP-002**: `FrameEventsProbeHandler` hiện có deterministic `overview_ref` / `crop_ref` và cache-first ordering; plan này phụ thuộc trực tiếp vào contract đó.
- **DEP-003**: `IMessageProducer` backend Redis/Kafka hiện tại phải hỗ trợ publish channel riêng cho ext-proc mà không trộn với `frame_events`.
- **DEP-004**: libcurl và nlohmann/json hiện đã có trong repo và đang được dùng bởi `ExternalProcessorService`; plan mới có thể tái sử dụng dependency runtime này.
- **DEP-005**: Downstream consumer muốn tận dụng correlation mới phải chấp nhận thêm các field `schema_version`, `frame_key`, `frame_ts_ms`, `instance_key`, `overview_ref`, `crop_ref` trong ext-proc message.

## 5. Files

- **FILE-001**: `/home/vms/Lantana/Dev/vms-engine/core/include/engine/core/config/config_types.hpp` - thêm config struct mới cho `frame_events.ext_processor`.
- **FILE-002**: `/home/vms/Lantana/Dev/vms-engine/infrastructure/config_parser/src/yaml_parser_handlers.cpp` - parse config nested mới và giữ nguyên parse path legacy.
- **FILE-003**: `/home/vms/Lantana/Dev/vms-engine/pipeline/include/engine/pipeline/extproc/frame_events_ext_proc_service.hpp` - public interface cho service mới.
- **FILE-004**: `/home/vms/Lantana/Dev/vms-engine/pipeline/src/extproc/frame_events_ext_proc_service.cpp` - implementation worker pool, HTTP, parse, publish.
- **FILE-005**: `/home/vms/Lantana/Dev/vms-engine/pipeline/include/engine/pipeline/evidence/frame_image_materializer.hpp` - helper cắt crop JPEG từ cached frame snapshot.
- **FILE-006**: `/home/vms/Lantana/Dev/vms-engine/pipeline/src/evidence/frame_image_materializer.cpp` - implementation materialization in-memory.
- **FILE-007**: `/home/vms/Lantana/Dev/vms-engine/pipeline/include/engine/pipeline/evidence/frame_evidence_cache.hpp` - mở rộng API resolve/read-only cho ext-proc worker nếu cần.
- **FILE-008**: `/home/vms/Lantana/Dev/vms-engine/pipeline/src/evidence/frame_evidence_cache.cpp` - hiện thực lookup/object extraction phục vụ ext-proc.
- **FILE-009**: `/home/vms/Lantana/Dev/vms-engine/pipeline/include/engine/pipeline/probes/frame_events_probe_handler.hpp` - thêm dependency injection cho service ext-proc mới.
- **FILE-010**: `/home/vms/Lantana/Dev/vms-engine/pipeline/src/probes/frame_events_probe_handler.cpp` - enqueue ext-proc jobs theo ordering chuẩn.
- **FILE-011**: `/home/vms/Lantana/Dev/vms-engine/pipeline/include/engine/pipeline/probes/probe_handler_manager.hpp` - wiring declaration nếu cần truyền dependency mới.
- **FILE-012**: `/home/vms/Lantana/Dev/vms-engine/pipeline/src/probes/probe_handler_manager.cpp` - wiring runtime cho `frame_events` branch.
- **FILE-013**: `/home/vms/Lantana/Dev/vms-engine/pipeline/include/engine/pipeline/pipeline_manager.hpp` - lifecycle ownership cho service mới.
- **FILE-014**: `/home/vms/Lantana/Dev/vms-engine/pipeline/src/pipeline_manager.cpp` - start/stop, shutdown order, dependency ownership.
- **FILE-015**: `/home/vms/Lantana/Dev/vms-engine/app/main.cpp` - startup wiring và logging cấu hình service.
- **FILE-016**: `/home/vms/Lantana/Dev/vms-engine/docs/configs/deepstream_default.yml` - YAML mẫu cho `frame_events.ext_processor`.
- **FILE-017**: `/home/vms/Lantana/Dev/vms-engine/docs/architecture/probes/frame_events_probe_handler.md` - docs cho flow ext-proc mới.
- **FILE-018**: `/home/vms/Lantana/Dev/vms-engine/docs/architecture/probes/ext_proc_svc.md` - phân tách tài liệu giữa legacy và service mới.
- **FILE-019**: `/home/vms/Lantana/Dev/vms-engine/docs/architecture/plans/phase2/01_deepstream_phase2.md` - thêm tham chiếu chéo tới plan mới này.

## 6. Testing

- **TEST-001**: Build validation: chạy build CMake đầy đủ sau khi thêm service mới và đảm bảo không có compile/link error ở `pipeline/`, `app/`, `infrastructure/config_parser/`.
- **TEST-002**: Legacy safety: bật `crop_objects.ext_processor` cũ nhưng tắt `frame_events.ext_processor`, xác minh behavior hiện tại của `ExternalProcessorService` không đổi.
- **TEST-003**: Frame-events sidecar enablement: bật `frame_events.ext_processor` và tắt `crop_objects.ext_processor`, xác minh `frame_events` vẫn publish bình thường dù external API timeout hoặc queue ext-proc đầy.
- **TEST-004**: Ordering check: xác minh log hoặc instrumentation phản ánh đúng thứ tự `store_frame -> publish_frame_message -> enqueue ext_proc`, không có ext-proc publish nào cho frame cache miss ngay sau semantic emit.
- **TEST-005**: Queue backpressure: cấu hình `queue_capacity` nhỏ, bắn burst object lớn, xác minh engine drop ext-proc jobs có log warning nhưng không giảm semantic throughput hoặc crash worker.
- **TEST-006**: Throttle correctness: với cùng `pipeline_id`, `source_id`, `object_id`, `label`, gửi nhiều frame gần nhau, xác minh ext-proc chỉ gọi HTTP theo `min_interval_sec` đã cấu hình.
- **TEST-007**: Payload compatibility: xác minh message ext-proc mới vẫn có đủ field legacy `event`, `pid`, `sid`, `sname`, `oid`, `class`, `conf`, `labels`, `result`, `display`, `event_ts`, đồng thời có thêm `schema_version`, `frame_key`, `frame_ts_ms`, `instance_key`.
- **TEST-008**: Empty-result policy: mock external API trả JSON không có `result_path`, chạy hai cấu hình `emit_empty_result = false` và `emit_empty_result = true`, xác minh publish behavior đúng theo config.
- **TEST-009**: Shutdown safety: dừng process khi còn ext-proc jobs đang xử lý, xác minh `stop()` drain hoặc cancel queue bounded, không để thread mồ côi và không crash teardown.
- **TEST-010**: Multi-pipeline routing: chạy worker với nhiều pipeline và source, xác minh ext-proc publish đúng `pipeline_id`, `source_id`, `source_name`, `frame_key`, `object_key` cho từng job, không lẫn correlation giữa pipeline.

## 7. Risks & Assumptions

- **RISK-001**: Nếu cache TTL quá ngắn hoặc cache miss xảy ra thường xuyên, ext-proc sidecar sẽ bị skip nhiều dù `frame_events` vẫn đúng. Cần theo dõi tỷ lệ cache hit cho ext-proc path.
- **RISK-002**: Worker pool bounded giúp tránh thread explosion nhưng có thể tạo backlog khi external API chậm hoặc số object nhiều. Queue metrics và log rate-limit là cần thiết.
- **RISK-003**: Reuse helper encode từ evidence path có thể làm coupling tăng nếu abstraction không tách đủ sạch giữa `write-to-disk` và `in-memory-bytes` flow.
- **RISK-004**: Giữ event name là `ext_proc` giúp tương thích downstream nhưng dễ gây nhầm giữa ext-proc legacy và ext-proc từ `frame_events` nếu schema_version không được dùng nhất quán.
- **ASSUMPTION-001**: `FrameEventsProbeHandler` và `FrameEvidenceCache` hiện tại đã đủ ổn định để làm nền cho ext-proc sidecar mới.
- **ASSUMPTION-002**: Downstream tiêu thụ ext-proc có thể chấp nhận thêm field mới mà không cần đổi discriminator event.
- **ASSUMPTION-003**: Use case của ext-proc cho `frame_events` là enrichment hậu semantic emit, không phải synchronous gate quyết định việc có emit `frame_events` hay không.

## 8. Related Specifications / Further Reading

- `/home/vms/Lantana/Dev/vms-engine/docs/architecture/plans/phase2/01_deepstream_phase2.md`
- `/home/vms/Lantana/Dev/vms-engine/docs/architecture/probes/frame_events_probe_handler.md`
- `/home/vms/Lantana/Dev/vms-engine/docs/architecture/probes/ext_proc_svc.md`
- `/home/vms/Lantana/Dev/vms-engine/pipeline/src/probes/frame_events_probe_handler.cpp`
- `/home/vms/Lantana/Dev/vms-engine/pipeline/src/probes/ext_proc_svc.cpp`
- `/home/vms/Lantana/Dev/vms-engine/pipeline/src/probes/crop_object_handler.cpp`
- `/home/vms/Lantana/Dev/vms-engine/pipeline/src/probes/probe_handler_manager.cpp`
- `/home/vms/Lantana/Dev/vms-engine/core/include/engine/core/config/config_types.hpp`
- `/home/vms/Lantana/Dev/vms-engine/infrastructure/config_parser/src/yaml_parser_handlers.cpp`
