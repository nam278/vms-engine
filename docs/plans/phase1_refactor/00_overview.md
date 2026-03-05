---
goal: "Phase 1 Master Plan — vms-engine greenfield refactor from lantanav2"
version: "1.0"
date_created: "2025-01-15"
last_updated: "2025-07-17"
owner: "VMS Engine Team"
status: "Planned"
tags: [architecture, refactor, master-plan, deepstream, c++17]
---

# Phase 1 — Master Plan

![Status: Planned](https://img.shields.io/badge/status-Planned-blue)

**vms-engine** is built **from scratch** (greenfield). Clean Architecture, DeepStream-only, `engine::` namespace. No migration from lantanav2 — every file is a new creation following these conventions.

## 1. Requirements & Constraints

- **REQ-001**: Greenfield build — all code written from scratch, no copy-paste from lantanav2
- **REQ-002**: Clean Architecture with 4 layers: core → pipeline → domain → infrastructure
- **REQ-003**: DeepStream SDK 8.0 as the only pipeline backend
- **REQ-004**: C++17 standard, CMake 3.16+, GStreamer 1.0
- **REQ-005**: `engine::` namespace everywhere — never use `lantana::`
- **CON-001**: No DLStreamer code paths — DeepStream-only
- **CON-002**: No `std::variant` backend wrappers — config structs are DeepStream-native, flat
- **CON-003**: No `#ifdef LANTANA_WITH_DEEPSTREAM` or `LANTANA_WITH_DLSTREAMER` guards
- **CON-004**: No DeepStream headers in `core/` — core depends only on std + GStreamer forward-declares
- **GUD-001**: Full config pattern — all builders receive `const engine::core::config::PipelineConfig&`; no config slicing
- **GUD-002**: RAII for all GStreamer resources — use `GstElementPtr`, `GstPadPtr`, `GstCapsPtr`, `GstBusPtr`, `GCharPtr`
- **GUD-003**: No static Redis coupling in handlers — `IMessageProducer*` injected via constructor
- **PAT-001**: Interface-first — define interfaces in `core/` before implementing in other layers

### Namespace Conventions

| Layer                               | Namespace                   |
| ----------------------------------- | --------------------------- |
| Interfaces + config + utils         | `engine::core::*`           |
| Pipeline builders, managers, probes | `engine::pipeline::*`       |
| Business rules                      | `engine::domain::*`         |
| Config parser, messaging, storage   | `engine::infrastructure::*` |

### Include Path Conventions

| Layer                    | Include prefix                         |
| ------------------------ | -------------------------------------- |
| Core interfaces          | `#include "engine/core/..."`           |
| Pipeline implementations | `#include "engine/pipeline/..."`       |
| Infrastructure adapters  | `#include "engine/infrastructure/..."` |
| Domain services          | `#include "engine/domain/..."`         |

### File Naming Rules

| Rule                             | Example                                                           |
| -------------------------------- | ----------------------------------------------------------------- |
| No `ds_` prefix on builder files | `source_builder.hpp` ✅ — NOT `ds_source_builder.hpp`             |
| No `ds_` prefix on manager files | `pipeline_manager.hpp` ✅ — NOT `ds_pipeline_manager.hpp`         |
| No `Ds` prefix on class names    | `BuilderFactory` ✅ — NOT `DsBuilderFactory`                      |
| No `_v2` suffix                  | `smart_record_handler.hpp` ✅ — NOT `smart_record_handler_v2.hpp` |
| All filenames `snake_case`       | `yaml_config_parser.cpp`                                          |

### Dependency Rule (strictly enforced)

```
app/          → may depend on: core, pipeline, domain, infrastructure
pipeline/     → depends on: core ONLY
domain/       → depends on: core ONLY
infrastructure/ → depends on: core ONLY
core/         → depends on: std library + GStreamer forward-declares ONLY
```

## 2. Implementation Steps

### Phase 1: Plan Overview & Execution Order

- GOAL-001: Define the complete execution plan for vms-engine Phase 1 refactor

| #   | File                                                     | Scope                                   | Files | Effort  |
| --- | -------------------------------------------------------- | --------------------------------------- | ----- | ------- |
| 01  | [01_project_scaffold.md](01_project_scaffold.md)         | CMake, dirs, Docker, configs            | ~15   | 1 day   |
| 02  | [02_core_layer.md](02_core_layer.md)                     | Interfaces, config types, utils         | ~45   | 2 days  |
| 03  | [03_pipeline_layer.md](03_pipeline_layer.md)             | Builders, manager, probes, events       | ~70   | 4 days  |
| 04  | [04_domain_layer.md](04_domain_layer.md)                 | Business rules, metadata parsing        | ~6    | 0.5 day |
| 05  | [05_infrastructure_layer.md](05_infrastructure_layer.md) | Config parser, messaging, storage, REST | ~25   | 2 days  |
| 06  | [06_services_app_layer.md](06_services_app_layer.md)     | main.cpp, plugins                       | ~11   | 0.5 day |
| 07  | [07_integration_testing.md](07_integration_testing.md)   | Build verify, runtime test, CI          | —     | 1 day   |

**Total estimated effort: ~11.5 days (1 developer)**

| Task     | Description                                  | Completed | Date |
| -------- | -------------------------------------------- | --------- | ---- |
| TASK-001 | Complete Plan 01 — Project Scaffold          |           |      |
| TASK-002 | Complete Plan 02 — Core Layer                |           |      |
| TASK-003 | Complete Plan 03 — Pipeline Layer            |           |      |
| TASK-004 | Complete Plan 04 — Domain Layer              |           |      |
| TASK-005 | Complete Plan 05 — Infrastructure Layer      |           |      |
| TASK-006 | Complete Plan 06 — App Layer & Plugins       |           |      |
| TASK-007 | Complete Plan 07 — Integration & Testing     |           |      |

### Execution Order & Dependencies

```
01 Project Scaffold ──────────────────────────────── (no deps)
       │
       ▼
02 Core Layer ────────────────────────────────────── (depends on: 01)
       │
       ├──────────────┬──────────────┐
       ▼              ▼              ▼
03 Pipeline      04 Domain     05 Infrastructure ── (all depend on: 02)
       │              │              │
       └──────────────┴──────────────┘
                      │
                      ▼
              06 App Layer ─────────────────────── (depends on: 03, 04, 05)
                      │
                      ▼
              07 Integration & Testing ───────────── (depends on: 06)
```

Plans 03, 04, 05 can be worked on **in parallel** since they only depend on core interfaces.

## 3. Alternatives

- **ALT-001**: Incremental migration from lantanav2 (rejected — clean break preferred for architectural consistency)
- **ALT-002**: Multi-backend support via `std::variant` wrappers (rejected — DeepStream-only simplifies codebase)
- **ALT-003**: Single monolithic CMakeLists.txt (rejected — per-layer CMake enables independent compilation)

## 4. Dependencies

- **DEP-001**: Docker image `vms-engine-dev:latest` with DeepStream 8.0 base
- **DEP-002**: DeepStream SDK 8.0 installed at `/opt/nvidia/deepstream/deepstream`
- **DEP-003**: GStreamer 1.14+ (system package inside DeepStream container)
- **DEP-004**: spdlog v1.14.1 (FetchContent)
- **DEP-005**: yaml-cpp v0.8.0 (FetchContent)
- **DEP-006**: hiredis v1.3.0 (FetchContent)
- **DEP-007**: nlohmann/json v3.11.3 (FetchContent)
- **DEP-008**: CMake 3.16+ with Ninja generator

## 5. Files

### File Count Summary

| Layer               | Headers (.hpp) | Sources (.cpp) | CMake | Notes                             |
| ------------------- | -------------- | -------------- | ----- | --------------------------------- |
| **core/**           | ~42            | ~3             | 1     | Interfaces, config types, utils   |
| **pipeline/**       | ~36            | ~33            | 1     | Builders, manager, probes, events |
| **domain/**         | 3              | 3              | 1     | Business rules, metadata types    |
| **infrastructure/** | ~6             | ~20            | 1     | Config parser, messaging, storage |
| **app/**            | 0              | 1              | 1     | main.cpp only                     |
| **plugins/**        | 0              | 7              | 1     | Custom NvDsInfer parsers          |
| **Root CMake**      | —              | —              | 1     | Top-level wiring                  |
| **Total**           | **~88**        | **~68**        | **8** |                                   |

- **FILE-001**: `CMakeLists.txt` (root) — top-level build configuration
- **FILE-002**: `core/CMakeLists.txt` — core layer build
- **FILE-003**: `pipeline/CMakeLists.txt` — pipeline layer build
- **FILE-004**: `domain/CMakeLists.txt` — domain layer build
- **FILE-005**: `infrastructure/CMakeLists.txt` — infrastructure layer build
- **FILE-006**: `app/CMakeLists.txt` — application entry point build
- **FILE-007**: `plugins/CMakeLists.txt` — plugins build
- **FILE-008**: `configs/default.yml` — default pipeline configuration

## 6. Testing

- **TEST-001**: Verify build after each plan — `cmake --build build -- -j5`
- **TEST-002**: Check zero `lantana` references — `grep -r "lantana" core/ pipeline/ domain/ infrastructure/ app/`
- **TEST-003**: Check zero backend guards — `grep -r "LANTANA_WITH_" --include="*.hpp" --include="*.cpp"`
- **TEST-004**: Check namespace consistency — all headers use `engine::` namespace
- **TEST-005**: Layer dependency compliance — core/ must not include pipeline/ or infrastructure/ headers
- **TEST-006**: Clean build in container — `rm -rf build && cmake configure && cmake build`
- **TEST-007**: Runtime smoke test — `./build/bin/vms_engine -c configs/default.yml`

## 7. Risks & Assumptions

- **RISK-001**: Missing DeepStream header in new path — **Mitigation**: Build after each plan; fix immediately
- **RISK-002**: Broken element linking — **Mitigation**: Test with simple single-source config first
- **RISK-003**: Handler registration fails — **Mitigation**: Test handler loading in Plan 07
- **RISK-004**: Config parser breaks with new schema — **Mitigation**: Parse test in Plan 05 with reference YAML
- **RISK-005**: GstBus message handling issues — **Mitigation**: Keep PipelineManager logic close to DS 8.0 samples
- **ASSUMPTION-001**: DeepStream SDK 8.0 is pre-installed in the dev container
- **ASSUMPTION-002**: All development and testing is done inside the Docker container
- **ASSUMPTION-003**: Plans 03, 04, 05 can be parallelized since they only depend on Plan 02

## 8. Related Specifications / Further Reading

- [AGENTS.md](../../../AGENTS.md) — Project overview and conventions
- [docs/architecture/deepstream/README.md](../../architecture/deepstream/README.md) — DeepStream architecture docs index
- [docs/architecture/RAII.md](../../architecture/RAII.md) — RAII guide for GStreamer/CUDA resources
- [docs/architecture/CMAKE.md](../../architecture/CMAKE.md) — Build system reference
- [docs/configs/deepstream_default.yml](../../configs/deepstream_default.yml) — Canonical YAML config
