# Phase 1 Refactor — Master Plan

> Refactor **lantanav2** → **vms-engine**: Clean Architecture, DeepStream-only, new namespace.

---

## Plan Files

| #  | File                                | Scope                              | Files | Effort  |
| -- | ----------------------------------- | ---------------------------------- | ----- | ------- |
| 01 | [01_project_scaffold.md](01_project_scaffold.md) | CMake, dirs, Docker, configs       | ~15   | 1 day   |
| 02 | [02_core_layer.md](02_core_layer.md)             | Interfaces, config types, utils    | ~45   | 2 days  |
| 03 | [03_pipeline_layer.md](03_pipeline_layer.md)     | Builders, manager, probes, events  | ~70   | 4 days  |
| 04 | [04_domain_layer.md](04_domain_layer.md)         | Business rules, metadata parsing   | ~6    | 0.5 day |
| 05 | [05_infrastructure_layer.md](05_infrastructure_layer.md) | Config parser, messaging, storage, REST | ~25 | 2 days |
| 06 | [06_services_app_layer.md](06_services_app_layer.md)     | Triton client, main.cpp, plugins   | ~12   | 1 day   |
| 07 | [07_integration_testing.md](07_integration_testing.md)    | Build verify, runtime test, CI     | —     | 1 day   |

**Total estimated effort: ~11.5 days (1 developer)**

---

## Execution Order & Dependencies

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
              06 Services & App ──────────────────── (depends on: 03, 04, 05)
                      │
                      ▼
              07 Integration & Testing ───────────── (depends on: 06)
```

Plans 03, 04, 05 can be worked on **in parallel** since they only depend on core interfaces.

---

## Global Refactoring Rules

### Namespace Change

| Before (lantanav2)                           | After (vms-engine)                          |
| -------------------------------------------- | ------------------------------------------- |
| `lantana::core::*`                           | `engine::core::*`                           |
| `lantana::backends::deepstream::*`           | `engine::pipeline::*`                       |
| `lantana::domain::*`                         | `engine::domain::*`                         |
| `lantana::infrastructure::*`                 | `engine::infrastructure::*`                 |
| `lantana::services::*`                       | `engine::services::*`                       |

### Include Path Change

| Before                                           | After                                     |
| ------------------------------------------------ | ----------------------------------------- |
| `#include "lantana/core/..."`                    | `#include "engine/core/..."`              |
| `#include "lantana/backends/deepstream/..."`     | `#include "engine/pipeline/..."`          |
| `#include "lantana/infrastructure/..."`          | `#include "engine/infrastructure/..."`    |
| `#include "lantana/domain/..."`                  | `#include "engine/domain/..."`            |
| `#include "lantana/services/..."`                | `#include "engine/services/..."`          |

### File Rename Rules

| Rule                                    | Example                                           |
| --------------------------------------- | ------------------------------------------------- |
| Drop `ds_` prefix from builder files    | `ds_source_builder.hpp` → `source_builder.hpp`    |
| Drop `ds_` prefix from manager files    | `ds_pipeline_manager.hpp` → `pipeline_manager.hpp`|
| Drop `ds_` prefix from factory          | `ds_builder_factory.hpp` → `builder_factory.hpp`  |
| Drop `Ds` prefix from class names       | `DsBuilderFactory` → `BuilderFactory`             |
| Drop `Ds` prefix from class names       | `DsPipelineManager` → `PipelineManager`           |

### Code Cleanup Rules

1. **Remove DLStreamer code** — Delete all `#ifdef LANTANA_WITH_DLSTREAMER` blocks
2. **Remove `std::variant` backend wrappers** — Replace with direct DeepStream types
3. **Remove `LANTANA_WITH_DEEPSTREAM` guards** — DeepStream is always on
4. **Fix IHandler Redis coupling** — Remove `static redis_producer_` from base interface
5. **Fix IPipelineManager backend leak** — Remove `#include` of deepstream headers in core

### Verification After Each Plan

After completing each plan, verify:

- [ ] `cmake -S . -B build` succeeds (configure)
- [ ] `cmake --build build -- -j$(nproc)` succeeds (compile)
- [ ] No `lantana` string in any `#include` directive
- [ ] No `lantana` namespace in any source file
- [ ] `grep -r "lantana" core/ pipeline/ domain/ infrastructure/ services/ app/` returns nothing

---

## File Count Summary

| Layer            | lantanav2 Files | vms-engine Files | Notes                      |
| ---------------- | --------------- | ---------------- | -------------------------- |
| **core/**        | 42 (.hpp) + 3 (.cpp) | 42 (.hpp) + 3 (.cpp) | Rename + clean interfaces |
| **pipeline/**    | 38 (.hpp) + 35 (.cpp) | ~36 (.hpp) + ~33 (.cpp) | Flatten, drop v1/v2 dups |
| **domain/**      | 3 (.hpp) + 2 (.cpp) | 3 (.hpp) + 3 (.cpp) | Fill empty files           |
| **infrastructure/** | 5 (.hpp) + 18 (.cpp) | 5 (.hpp) + 18 (.cpp) | Rename only           |
| **services/**    | 1 (.hpp) + 1 (.cpp) | 1 (.hpp) + 1 (.cpp) | Rename only               |
| **app/**         | 1 (.cpp)        | 1 (.cpp)         | Rewrite wiring             |
| **plugins/**     | 7 (.cpp)        | 7 (.cpp)         | Copy, minimal changes      |
| **CMake**        | 7 files         | 7 files          | Rewrite from scratch       |
| **Configs**      | ~5 .yml         | ~5 .yml          | Rename references          |
| **Docker**       | 3 files         | 3 files          | Update paths               |
| **Total**        | ~170            | ~165             |                            |

---

## Risk Register

| Risk                                      | Impact  | Mitigation                                         |
| ----------------------------------------- | ------- | -------------------------------------------------- |
| Missing DeepStream header in new path     | Build   | Build after each plan; fix immediately              |
| Broken element linking after rename       | Runtime | Test with simple single-source config first         |
| Handler registration fails                | Runtime | Test handler loading in Plan 07                     |
| Config parser breaks with renamed structs | Runtime | Parse test in Plan 05 with existing YAML files      |
| GstBus message handling changes           | Runtime | Keep PipelineManager logic identical, only rename   |
