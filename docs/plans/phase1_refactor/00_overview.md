# Phase 1 — Master Plan

> **vms-engine** is built **from scratch** (greenfield). Clean Architecture, DeepStream-only, `engine::` namespace.
> No migration from lantanav2 — every file is a new creation following these conventions.

---

## Plan Files

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
              06 App Layer ─────────────────────── (depends on: 03, 04, 05)
                      │
                      ▼
              07 Integration & Testing ───────────── (depends on: 06)
```

Plans 03, 04, 05 can be worked on **in parallel** since they only depend on core interfaces.

---

## Global Architecture Rules

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

### Code Standards

1. **DeepStream-only** — No DLStreamer code paths
2. **No `std::variant` backend wrappers** — Config structs are DeepStream-native, flat
3. **No backend guards** — No `#ifdef LANTANA_WITH_DEEPSTREAM` or `LANTANA_WITH_DLSTREAMER`; DeepStream is always on
4. **No static Redis coupling in handlers** — `IMessageProducer*` injected via constructor
5. **No DeepStream headers in `core/`** — Core depends only on std + GStreamer forward-declares
6. **Full config pattern** — All builders receive `const engine::core::config::PipelineConfig&`; no config slicing
7. **RAII for all GStreamer resources** — Use `GstElementPtr`, `GstPadPtr`, `GstCapsPtr`, `GstBusPtr`, `GCharPtr` from `engine::core::utils`; never leave raw `GstElement*` unguarded on error paths
8. **`engine::` namespace everywhere** — Never use `lantana::` (old name)

### Dependency Rule (strictly enforced)

```
app/          → may depend on: core, pipeline, domain, infrastructure
pipeline/     → depends on: core ONLY
domain/       → depends on: core ONLY
infrastructure/ → depends on: core ONLY
core/         → depends on: std library + GStreamer forward-declares ONLY
```

### Verification After Each Plan

After completing each plan, verify inside the dev container:

```bash
# Inside container: docker compose exec app bash
cd /opt/vms_engine

# Configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream -G Ninja

# Build
cmake --build build -- -j5

# Check for namespace violations
grep -r "lantana" core/ pipeline/ domain/ infrastructure/ app/ \
    --include="*.hpp" --include="*.cpp" 2>/dev/null && echo "FAIL: lantana found" || echo "PASS"
```

---

## File Count Summary

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

---

## Risk Register

| Risk                                  | Impact  | Mitigation                                         |
| ------------------------------------- | ------- | -------------------------------------------------- |
| Missing DeepStream header in new path | Build   | Build after each plan; fix immediately             |
| Broken element linking                | Runtime | Test with simple single-source config first        |
| Handler registration fails            | Runtime | Test handler loading in Plan 07                    |
| Config parser breaks with new schema  | Runtime | Parse test in Plan 05 with a reference YAML file   |
| GstBus message handling issues        | Runtime | Keep PipelineManager logic close to DS 8.0 samples |
