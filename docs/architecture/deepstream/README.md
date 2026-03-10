# VMS Engine — DeepStream Architecture

> Tài liệu kỹ thuật cho kiến trúc **DeepStream pipeline** của VMS Engine.

---

## Mục lục

| #   | File                                                                         | Nội dung                                                                                  |
| --- | ---------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------- |
| 00  | [00_project_overview.md](00_project_overview.md)                             | Tổng quan dự án, tech stack, pipeline flow                                                |
| 01  | [01_directory_structure.md](01_directory_structure.md)                       | Cấu trúc thư mục chi tiết                                                                 |
| 02  | [02_core_interfaces.md](02_core_interfaces.md)                               | Core interfaces & contracts (`IPipelineManager`, `IBuilderFactory`, v.v.)                 |
| 03  | [03_pipeline_building.md](03_pipeline_building.md)                           | Quy trình build pipeline theo 5 phases                                                    |
| 04  | [04_linking_system.md](04_linking_system.md)                                 | Hệ thống liên kết elements & queue insertion                                              |
| 05  | [05_configuration.md](05_configuration.md)                                   | Cấu hình YAML — schema, conventions, ví dụ                                                |
| 06  | [06_runtime_lifecycle.md](06_runtime_lifecycle.md)                           | Vòng đời runtime: GstBus, state machine, error handling                                   |
| 07  | [07_event_handlers_probes.md](07_event_handlers_probes.md)                   | GStreamer pad probes, `ProbeHandlerManager`, SmartRecord / CropObjects / ClassIdNamespace |
| 08  | [08_analytics.md](08_analytics.md)                                           | Analytics: ROI, line crossing, `nvdsanalytics`                                            |
| 09  | [09_outputs_smart_record.md](09_outputs_smart_record.md)                     | Output sinks & Smart Record                                                               |
| 10  | [10_rest_api.md](10_rest_api.md)                                             | DeepStream REST API — quản lý stream động (add/remove camera lúc runtime)                 |
| 11  | [11_runtime_element_control.md](11_runtime_element_control.md)               | Runtime control tổng quát cho element properties; use case đầu tiên là `nvdsosd`          |
| 12  | [12_kafka_deepstream_compatibility.md](12_kafka_deepstream_compatibility.md) | Kafka integration strategy để tương thích với DeepStream REST trong cùng process          |

---

## Tài liệu Probe Handler chuyên sâu

| File                                                                                   | Nội dung                                                                                |
| -------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| [../probes/class_id_namespacing_handler.md](../probes/class_id_namespacing_handler.md) | Multi-GIE class_id namespacing — offset trước tracker, restore sau tracker              |
| [../probes/smart_record_probe_handler.md](../probes/smart_record_probe_handler.md)     | SmartRecord probe — GstClockTime timing, stale cache, concurrent limit, session cleanup |
| [../probes/crop_object_handler.md](../probes/crop_object_handler.md)                   | CropObject probe — batch-accumulate, PubDecisionType, payload hash dedup                |
| [../probes/ext_proc_svc.md](../probes/ext_proc_svc.md)                                 | ExternalProcessorService — HTTP AI enrichment (face-rec), CURL multipart, throttle      |

---

## Tài liệu liên quan (root)

| File                                                         | Nội dung                                      |
| ------------------------------------------------------------ | --------------------------------------------- |
| [../ARCHITECTURE_BLUEPRINT.md](../ARCHITECTURE_BLUEPRINT.md) | Blueprint kiến trúc tổng thể                  |
| [../RAII.md](../RAII.md)                                     | RAII patterns & GStreamer resource management |
| [../CMAKE.md](../CMAKE.md)                                   | Hướng dẫn CMake build system                  |

---

## Đọc theo thứ tự

### Mới bắt đầu?

> 📖 `00` → `01` → `02` → `03`

### Cần triển khai feature mới?

> 🛠️ `03` (phases) → `04` (linking) → `05` (config) → `07` (probes)

### Quản lý camera động (add/remove lúc runtime)?

> 🔌 `10` (REST API) + `06` (lifecycle)

### Debug runtime issues?

> 🐛 `06` (lifecycle) → `07` (probes) → [RAII.md](../RAII.md)

### Cần hiểu probe handler cụ thể?

> 🔬 `07` (tổng quan) → chọn probe doc trong bảng **Probe Handler chuyên sâu**
