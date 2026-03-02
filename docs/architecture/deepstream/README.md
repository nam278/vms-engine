# VMS Engine — DeepStream Architecture

Tài liệu kỹ thuật cho kiến trúc **DeepStream pipeline** của VMS Engine.

---

## Mục lục

| # | File | Nội dung |
|---|------|----------|
| 00 | [00_project_overview.md](00_project_overview.md) | Tổng quan dự án, tech stack, pipeline flow |
| 01 | [01_directory_structure.md](01_directory_structure.md) | Cấu trúc thư mục chi tiết |
| 02 | [02_core_interfaces.md](02_core_interfaces.md) | Core interfaces & contracts (IPipelineManager, IBuilderFactory, v.v.) |
| 03 | [03_pipeline_building.md](03_pipeline_building.md) | Quy trình build pipeline theo phases |
| 04 | [04_linking_system.md](04_linking_system.md) | Hệ thống liên kết elements & queue insertion |
| 05 | [05_configuration.md](05_configuration.md) | Cấu hình YAML — schema, conventions, ví dụ |
| 06 | [06_runtime_lifecycle.md](06_runtime_lifecycle.md) | Vòng đời runtime: GstBus, state machine, error handling |
| 07 | [07_event_handlers_probes.md](07_event_handlers_probes.md) | Event handlers & GStreamer pad probes |
| 08 | [08_analytics.md](08_analytics.md) | Analytics: ROI, line crossing, nvdsanalytics |
| 09 | [09_outputs_smart_record.md](09_outputs_smart_record.md) | Outputs sinks & Smart Record |
| 10 | [10_signal_vs_probe_deep_dive.md](10_signal_vs_probe_deep_dive.md) | Signal vs Probe — khi nào dùng cái nào |

---

## Tài liệu liên quan

- [`../ARCHITECTURE_BLUEPRINT.md`](../ARCHITECTURE_BLUEPRINT.md) — Blueprint kiến trúc tổng thể
- [`../RAII.md`](../RAII.md) — RAII patterns & GStreamer resource management
- [`../CMAKE.md`](../CMAKE.md) — Hướng dẫn CMake build system
- [`../../configs/deepstream_default.yml`](../../configs/deepstream_default.yml) — Config mẫu đầy đủ

---

## Đọc theo thứ tự

**Mới bắt đầu?** → Đọc `00` → `01` → `02` → `03`

**Cần triển khai feature?** → `03` (phases) + `04` (linking) + `05` (config)

**Debug runtime issues?** → `06` (lifecycle) + `07` (probes)
