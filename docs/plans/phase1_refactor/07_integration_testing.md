---
goal: "Plan 07 — Integration Testing and Verification"
version: "1.0"
date_created: "2025-01-15"
last_updated: "2025-07-17"
owner: "VMS Engine Team"
status: "Planned"
tags: [testing, integration, verification, ci, static-analysis, runtime, feature-matrix]
---

# Plan 07 — Integration Testing and Verification

![Status: Planned](https://img.shields.io/badge/status-Planned-blue)

End-to-end verification that the fully-built vms-engine builds, runs, and processes video.
This plan is executed **after all implementation (Plans 01–06) is complete**.

---

## 1. Requirements & Constraints

- **REQ-001**: Plans 01–06 completed — all libraries and binary compile.
- **REQ-002**: Full build succeeds — `cmake --build build -- -j5`.
- **REQ-003**: All targets: `vms_engine_core`, `vms_engine_pipeline`, `vms_engine_domain`, `vms_engine_infrastructure`, `vms_engine` binary, 7 plugin `.so` files.
- **REQ-004**: Zero old references (`lantana`, `LANTANA_WITH_*`, `DLStreamer`, `ds_` prefixes) in source.
- **REQ-005**: Namespace consistency — `engine::core`, `engine::pipeline`, `engine::domain`, `engine::infrastructure` throughout all layers.
- **SEC-001**: Layer dependency rule enforced — core imports nothing, domain imports core only.
- **CON-001**: GPU runner required for runtime smoke tests (DeepStream needs NVIDIA GPU).
- **CON-002**: CI pipeline is optional — static checks can run without GPU.
- **GUD-001**: Each phase can be executed independently — no cumulative state between phases.
- **GUD-002**: Test configs (`test_minimal.yml`, `test_single_source.yml`) committed to `configs/`.

---

## 2. Implementation Steps

### Phase 1 — Static Analysis (No Runtime)

**GOAL-001**: Verify zero legacy references and full namespace/layer compliance.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-001 | Zero old references — `grep -rn "lantana"` in source dirs returns 0 results | ☐ | |
| TASK-002 | Zero backend guards — `grep -rn "LANTANA_WITH_DEEPSTREAM\|LANTANA_WITH_DLSTREAMER"` returns 0 | ☐ | |
| TASK-003 | Zero DLStreamer references — `grep -rn "dlstreamer\|DLStreamer\|DLSTREAMER"` returns 0 | ☐ | |
| TASK-004 | Zero `ds_` prefixes — `find` for `ds_*` files and `grep` for `Ds[A-Z]` class names return 0 | ☐ | |
| TASK-005 | Namespace consistency — all layer headers contain correct `engine::*` namespace | ☐ | |
| TASK-006 | Layer dependency compliance — core/domain do NOT include pipeline/infrastructure headers | ☐ | |

#### Static Analysis Commands

```bash
# TASK-001: Zero old references
grep -rn "lantana" \
    core/ pipeline/ domain/ infrastructure/ app/ plugins/ \
    --include="*.hpp" --include="*.cpp" --include="*.h" \
    | grep -v "// lantana" | grep -v "CHANGELOG" | grep -v ".md"
# Expected: 0 results

# TASK-002: Zero backend guards
grep -rn "LANTANA_WITH_DEEPSTREAM\|LANTANA_WITH_DLSTREAMER\|LANTANA_WITH_" \
    core/ pipeline/ domain/ infrastructure/ app/ \
    --include="*.hpp" --include="*.cpp"
# Expected: 0 results

# TASK-003: Zero DLStreamer references
grep -rn "dlstreamer\|DLStreamer\|DLSTREAMER\|dl_pipeline" \
    --include="*.hpp" --include="*.cpp" --include="*.cmake" --include="CMakeLists.txt" \
    core/ pipeline/ domain/ infrastructure/ app/
# Expected: 0 results

# TASK-004: Zero ds_ prefixes (excluding plugins & DeepStream SDK types)
find core/ pipeline/ domain/ infrastructure/ app/ -name "ds_*"
grep -rn "\bDs[A-Z][a-zA-Z]*Builder\|DsPipeline\|DsRuntime\|DsConfig\|DsSmart\|DsBuilder" \
    --include="*.hpp" --include="*.cpp" \
    core/ pipeline/ domain/ infrastructure/ app/
# Expected: 0 results for both

# TASK-005: Namespace consistency
grep -rL "engine::core" core/include/ --include="*.hpp" | head -20
grep -rL "engine::pipeline" pipeline/include/ --include="*.hpp" | head -20
grep -rL "engine::infrastructure" infrastructure/*/include/ --include="*.hpp" | head -20
grep -rL "engine::domain" domain/include/ --include="*.hpp" | head -20

# TASK-006: Layer dependency compliance
grep -rn '#include "engine/pipeline\|#include "engine/infrastructure' \
    core/ --include="*.hpp" --include="*.cpp"
grep -rn '#include "engine/pipeline\|#include "engine/infrastructure' \
    domain/ --include="*.hpp" --include="*.cpp"
# Expected: 0 results for both
```

---

### Phase 2 — Build Verification

**GOAL-002**: Clean build succeeds, all targets compile, binary and plugins are produced.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-007 | Clean build from scratch — `rm -rf build && cmake configure && cmake build` | ☐ | |
| TASK-008 | Individual target build — each of the 8 library/binary targets compiles | ☐ | |
| TASK-009 | Plugin build — all 7 `.so` files produced | ☐ | |
| TASK-010 | Binary check — `file` and `ldd` confirm executable with all deps resolved | ☐ | |

#### Build Commands

```bash
# TASK-007: Clean build
cd /opt/vms_engine
rm -rf build
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream \
    -G Ninja
cmake --build build -- -j5

# TASK-008: Individual targets
cmake --build build --target vms_engine_core -- -j5
cmake --build build --target vms_engine_domain -- -j5
cmake --build build --target vms_engine_pipeline -- -j5
cmake --build build --target vms_engine_config_parser -- -j5
cmake --build build --target vms_engine_messaging -- -j5
cmake --build build --target vms_engine_storage -- -j5
cmake --build build --target vms_engine_rest_api -- -j5
cmake --build build --target vms_engine -- -j5

# TASK-009: Plugins
cmake --build build -- -j5
find build/ -name "*.so" | sort
# Expected: 7 .so files

# TASK-010: Binary check
file build/bin/vms_engine
ldd build/bin/vms_engine | grep -i "not found"
# Expected: no "not found" lines
```

---

### Phase 3 — Runtime Smoke Tests

**GOAL-003**: Binary starts, loads config, processes video, and shuts down gracefully.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-011 | Help / argument parsing — `--help` does not crash | ☐ | |
| TASK-012 | Config load — `test_minimal.yml` loads and logs summary | ☐ | |
| TASK-013 | Single source pipeline — `test_single_source.yml` processes frames for 30s | ☐ | |
| TASK-014 | Signal handling — SIGINT triggers graceful shutdown with exit code 0 | ☐ | |

#### Test Configs

**test_minimal.yml** — Config load test (no cameras):

```yaml
version: "1.0"
pipeline:
  id: test
  name: vms_engine_test
  log_level: DEBUG

queue_defaults:
  max_size_buffers: 3
  leaky: 2

sources:
  type: nvmultiurisrcbin
  max_batch_size: 1
  gpu_id: 0
  width: 1280
  height: 720
  cameras: []

processing:
  elements: []

visuals:
  enable: false
  elements: []

outputs: []
event_handlers: []
```

**test_single_source.yml** — Single source pipeline test:

```yaml
version: "1.0"
pipeline:
  id: smoke_test
  name: smoke_test
  log_level: DEBUG

queue_defaults:
  max_size_buffers: 5
  leaky: 2

sources:
  type: nvmultiurisrcbin
  max_batch_size: 1
  gpu_id: 0
  width: 1920
  height: 1080
  live_source: false
  cameras:
    - id: test_cam
      uri: "file:///path/to/test_video.mp4"

processing:
  elements:
    - id: pgie
      type: nvinfer
      role: primary_inference
      config_file: "/opt/engine/data/components/pgie/config.yml"
      process_mode: 1
      batch_size: 1
      queue: {}

visuals:
  enable: false
  elements: []

outputs:
  - id: sink_0
    type: fakesink
    elements:
      - id: fake
        type: fakesink

event_handlers: []
```

#### Smoke Test Commands

```bash
# TASK-011: Help / argument parsing
./build/bin/vms_engine --help 2>&1 || true
# Expected: prints usage or unknown argument error (no crash)

# TASK-012: Config load
./build/bin/vms_engine -c test_minimal.yml
# Expected: "Configuration loaded successfully", exits cleanly (no cameras)

# TASK-013: Single source pipeline
timeout 30 ./build/bin/vms_engine -c test_single_source.yml
# Expected: pipeline starts, processes frames, exits after 30s

# TASK-014: Signal handling
./build/bin/vms_engine -c test_single_source.yml &
PID=$!
sleep 5
kill -SIGINT $PID
wait $PID
echo "Exit code: $?"
# Expected: graceful shutdown, exit code 0
```

---

### Phase 4 — Feature Checklist

**GOAL-004**: Validate all 22 features in the feature matrix.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-015 | Validate all 22 features listed in the feature status matrix | ☐ | |

#### Feature Status Matrix

| # | Feature | Status | How to Test |
|---|---------|--------|-------------|
| 1 | RTSP source ingestion | ☐ | RTSP stream → fakesink config |
| 2 | File source ingestion | ☐ | MP4 file → fakesink config |
| 3 | Multi-source muxing | ☐ | 2+ cameras in config |
| 4 | Primary inference (nvinfer) | ☐ | PGIE with model config |
| 5 | Secondary inference | ☐ | SGIE chained after PGIE |
| 6 | Object tracking | ☐ | nvtracker in processing elements |
| 7 | OSD (display overlay) | ☐ | nvdsosd enabled in visuals |
| 8 | Tiler (multi-view grid) | ☐ | nvmultistreamtiler with batch_size > 1 |
| 9 | RTSP output sink | ☐ | rtspclientsink in outputs |
| 10 | File output sink | ☐ | filesink in outputs |
| 11 | Smart recording | ☐ | `smart_record: 2` in sources config |
| 12 | Event handling (crop+save) | ☐ | Custom handler with storage |
| 13 | Redis stream publishing | ☐ | Messaging config + Redis running |
| 14 | REST API control | ☐ | REST API enabled, curl test |
| 15 | S3 storage upload | ☐ | S3 storage config + event trigger |
| 16 | Local storage save | ☐ | Local storage config + event trigger |
| 17 | Triton inference | ☐ | External service config |
| 18 | Analytics (nvdsanalytics) | ☐ | Analytics config in processing elements |
| 19 | Graceful shutdown (SIGINT) | ☐ | `kill -SIGINT PID` |
| 20 | Crash recovery (SIGSEGV) | ☐ | Deliberate segfault test |
| 21 | Runtime param update | ☐ | REST API endpoint |
| 22 | Runtime stream add/remove | ☐ | REST API endpoint |

---

### Phase 5 — CI Pipeline Setup (Optional)

**GOAL-005**: Automated build and static checks in CI.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-016 | Create `Dockerfile.ci` for build + static checks | ☐ | |
| TASK-017 | Create GitHub Actions workflow (`.github/workflows/build.yml`) | ☐ | |

#### Dockerfile.ci

```dockerfile
FROM nvcr.io/nvidia/deepstream:8.0-gc-triton-devel

RUN apt-get update && apt-get install -y \
    cmake ninja-build build-essential pkg-config \
    libglib2.0-dev libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/vms_engine
COPY . .

RUN cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream \
    -G Ninja \
    && cmake --build build -- -j5

# Static checks
RUN ! grep -rn "lantana" core/ pipeline/ domain/ infrastructure/ app/ \
    --include="*.hpp" --include="*.cpp" | grep -v "// lantana"
```

#### GitHub Actions

```yaml
# .github/workflows/build.yml
name: Build & Verify
on: [push, pull_request]
jobs:
  build:
    runs-on: self-hosted  # GPU runner with DeepStream SDK
    steps:
      - uses: actions/checkout@v4
      - name: Build
        run: |
          cmake -S . -B build \
            -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream \
            -G Ninja
          cmake --build build -- -j5
      - name: Static Checks
        run: |
          ! grep -rn "lantana" core/ pipeline/ domain/ infrastructure/ app/ \
            --include="*.hpp" --include="*.cpp" | grep -v "//"
          ! grep -rn "LANTANA_WITH_" \
            --include="*.hpp" --include="*.cpp" --include="CMakeLists.txt"
```

---

## 3. Alternatives

- **ALT-001**: GoogleTest / Catch2 unit tests (deferred — no test framework in build currently; manual verification via shell scripts for Phase 1 refactor).
- **ALT-002**: Container-based runtime tests via Docker Compose (deferred — requires GPU passthrough setup).
- **ALT-003**: Pre-commit hooks for static analysis (viable alternative to CI pipeline for early catch).

---

## 4. Dependencies

- **DEP-001**: Plans 01–06 completed — all source files and CMake targets exist.
- **DEP-002**: NVIDIA GPU with DeepStream SDK 8.0 for runtime smoke tests.
- **DEP-003**: Test video file (`test_video.mp4`) for Phase 3 single-source test.
- **DEP-004**: Redis server running for Feature #13 (messaging test).
- **DEP-005**: RTSP source or MediaMTX for Feature #1 (RTSP ingestion test).

---

## 5. Files

| ID | File Path | Description |
|----|-----------|-------------|
| FILE-001 | `configs/test_minimal.yml` | Minimal config — no cameras, config load test |
| FILE-002 | `configs/test_single_source.yml` | Single file source with PGIE and fakesink |
| FILE-003 | `Dockerfile.ci` | CI build + static checks Docker image |
| FILE-004 | `.github/workflows/build.yml` | GitHub Actions build & verify workflow |

---

## 6. Testing & Verification

This plan IS the testing plan. Verification of this plan is done by running through all 5 phases:

- **TEST-001**: Phase 1 — All 6 static analysis checks return 0 violations.
- **TEST-002**: Phase 2 — Clean build succeeds, all 8 targets + 7 plugins compile.
- **TEST-003**: Phase 3 — All 4 runtime smoke tests pass (help, config load, single source, SIGINT).
- **TEST-004**: Phase 4 — All 22 features checked in feature status matrix.
- **TEST-005**: Phase 5 — CI pipeline builds and static checks pass (optional).

---

## 7. Risks & Assumptions

- **RISK-001**: Runtime tests require GPU — cannot run in standard CI; mitigated by self-hosted runner.
- **RISK-002**: Test video file may not be available in CI; mitigated by file source with `file-loop: true`.
- **RISK-003**: Feature matrix items depend on external services (Redis, RTSP, S3); test incrementally.
- **ASSUMPTION-001**: DeepStream SDK 8.0 is installed at `/opt/nvidia/deepstream/deepstream` in all environments.
- **ASSUMPTION-002**: Self-hosted CI runner has NVIDIA GPU with appropriate drivers.
- **ASSUMPTION-003**: `configs/` directory is included in build output via CMake `file(COPY ...)`.

---

## 8. Related Specifications

- [Plan 00 — Overview](00_overview.md)
- [Plan 01 — Project Scaffold](01_project_scaffold.md)
- [Plan 02 — Core Layer](02_core_layer.md)
- [Plan 03 — Pipeline Layer](03_pipeline_layer.md)
- [Plan 04 — Domain Layer](04_domain_layer.md)
- [Plan 05 — Infrastructure Layer](05_infrastructure_layer.md)
- [Plan 06 — Application Entry Point and Plugins](06_services_app_layer.md)
- [AGENTS.md](../../AGENTS.md)

---

## Final Completion Checklist

- [ ] Phase 1: All 6 static analysis checks pass (0 violations)
- [ ] Phase 2: Clean build succeeds, all targets compile, binary runs
- [ ] Phase 3.1: Argument parsing works (no crash)
- [ ] Phase 3.2: Config load works (logs summary)
- [ ] Phase 3.3: Single source pipeline runs (frames processed)
- [ ] Phase 3.4: Signal handling works (graceful shutdown)
- [ ] Phase 4: All 22 features checked in feature matrix
- [ ] Layer dependency rule enforced (core → ∅, domain → core only)
- [ ] No `ds_` prefixed files or classes in engine code
- [ ] `engine::` namespace used throughout all layers
- [ ] configs/ directory has `default.yml` and example test configs
- [ ] `docs/configs/deepstream_default.yml` matches implemented parser schema
