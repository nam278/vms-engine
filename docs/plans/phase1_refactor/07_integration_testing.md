# Plan 07 — Integration Testing & Verification

> End-to-end verification that the fully-migrated vms-engine builds, runs, and behaves identically to lantanav2.
> This plan is executed **after all code migration (Plans 01–06) is complete**.

---

## Prerequisites

- Plans 01–06 completed
- `cmake --build build -- -j$(nproc)` succeeds (full build)
- All targets: `vms_engine_core`, `vms_engine_pipeline`, `vms_engine_domain`, `vms_engine_infrastructure`, `vms_engine_services`, `vms_engine` (binary), 7 plugin `.so` files

---

## Phase 1: Static Analysis (No Runtime)

### 1.1 — Zero Old References

```bash
# Check entire source tree for any remaining lantana references
grep -rn "lantana" \
    core/ pipeline/ domain/ infrastructure/ services/ app/ plugins/ \
    --include="*.hpp" --include="*.cpp" --include="*.h" \
    | grep -v "// lantana" | grep -v "CHANGELOG" | grep -v ".md"
# Expected: 0 results
```

### 1.2 — Zero Backend Guards

```bash
grep -rn "LANTANA_WITH_DEEPSTREAM\|LANTANA_WITH_DLSTREAMER\|LANTANA_WITH_" \
    core/ pipeline/ domain/ infrastructure/ services/ app/ \
    --include="*.hpp" --include="*.cpp"
# Expected: 0 results
```

### 1.3 — Zero DLStreamer References

```bash
grep -rn "dlstreamer\|DLStreamer\|DLSTREAMER\|dl_pipeline" \
    --include="*.hpp" --include="*.cpp" --include="*.cmake" --include="CMakeLists.txt" \
    core/ pipeline/ domain/ infrastructure/ services/ app/
# Expected: 0 results
```

### 1.4 — Zero ds_ Prefixes (Excluding Plugins & DeepStream SDK Includes)

```bash
# Check for ds_ prefixed file names
find core/ pipeline/ domain/ infrastructure/ services/ app/ -name "ds_*"
# Expected: 0 results

# Check for Ds class name prefixes (excluding DeepStream SDK types like NvDs*)
grep -rn "\bDs[A-Z][a-zA-Z]*Builder\|DsPipeline\|DsRuntime\|DsConfig\|DsSmart\|DsBuilder" \
    --include="*.hpp" --include="*.cpp" \
    core/ pipeline/ domain/ infrastructure/ services/ app/
# Expected: 0 results
```

### 1.5 — Namespace Consistency

```bash
# All headers in core/ must have engine::core
grep -rL "engine::core" core/include/ --include="*.hpp" | head -20

# All headers in pipeline/ must have engine::pipeline
grep -rL "engine::pipeline" pipeline/include/ --include="*.hpp" | head -20

# All headers in infrastructure/ must have engine::infrastructure
grep -rL "engine::infrastructure" infrastructure/*/include/ --include="*.hpp" | head -20

# All headers in domain/ must have engine::domain
grep -rL "engine::domain" domain/include/ --include="*.hpp" | head -20
```

### 1.6 — Layer Dependency Compliance

```bash
# Core must NOT include pipeline, infrastructure, or services headers
grep -rn '#include "engine/pipeline\|#include "engine/infrastructure\|#include "engine/services' \
    core/ --include="*.hpp" --include="*.cpp"
# Expected: 0 results

# Domain must NOT include pipeline, infrastructure, or services headers
grep -rn '#include "engine/pipeline\|#include "engine/infrastructure\|#include "engine/services' \
    domain/ --include="*.hpp" --include="*.cpp"
# Expected: 0 results
```

---

## Phase 2: Build Verification

### 2.1 — Clean Build

```bash
rm -rf build/*
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream
cmake --build build -- -j$(nproc)
```

### 2.2 — Individual Target Build

```bash
cmake --build build --target vms_engine_core -- -j$(nproc)
cmake --build build --target vms_engine_domain -- -j$(nproc)
cmake --build build --target vms_engine_pipeline -- -j$(nproc)
cmake --build build --target vms_engine_config_parser -- -j$(nproc)
cmake --build build --target vms_engine_messaging -- -j$(nproc)
cmake --build build --target vms_engine_storage -- -j$(nproc)
cmake --build build --target vms_engine_rest_api -- -j$(nproc)
cmake --build build --target vms_engine_services -- -j$(nproc)
cmake --build build --target vms_engine -- -j$(nproc)
```

### 2.3 — Plugin Build

```bash
# All 7 plugins should produce .so files
cmake --build build -- -j$(nproc)
find build/ -name "*.so" | sort
# Expected: 7 .so files
```

### 2.4 — Binary Check

```bash
# Binary exists and is executable
file build/bin/vms_engine
ldd build/bin/vms_engine | grep -i "not found"
# Expected: no "not found" lines (all deps resolved)
```

---

## Phase 3: Runtime Smoke Tests

### 3.1 — Help / Argument Parsing

```bash
# Should show usage or accept --help
./build/bin/vms_engine --help 2>&1 || true
# Expected: prints usage or unknown argument error (not crash)
```

### 3.2 — Config Load Test

Create a minimal test config:

```yaml
# test_config.yml
version: "1.0"
application:
  name: vms_engine_test
  log_level: DEBUG
  log_file: ""
  backend:
    type: deepstream
sources: []
processing_flow: []
outputs: []
```

```bash
./build/bin/vms_engine -c test_config.yml
# Expected: loads config, logs summary, may fail with "no sources" but should NOT crash
# Should see: "Configuration loaded successfully"
```

### 3.3 — Single Source Pipeline Test

Use a test config with a single RTSP or file source:

```yaml
# test_single_source.yml
version: "1.0"
application:
  name: vms_engine_test
  log_level: DEBUG
  backend:
    type: deepstream

sources:
  - id: "src_0"
    type: rtsp
    uri: "rtsp://localhost:8554/test"
    # OR for file:
    # type: file
    # uri: "/path/to/test_video.mp4"

stream_muxer:
  id: "muxer_0"
  enable: true
  width: 1920
  height: 1080
  batch_size: 1

processing_flow:
  - id: "pgie_0"
    type: nvinfer
    config_path: "models/config_infer_primary.txt"

outputs:
  - id: "sink_0"
    type: fakesink
```

```bash
timeout 30 ./build/bin/vms_engine -c test_single_source.yml
# Expected: pipeline starts, processes frames, exits cleanly on timeout
# Watch for: "Pipeline started successfully" in logs
```

### 3.4 — Signal Handling Test

```bash
# Start in background, send SIGINT
./build/bin/vms_engine -c test_single_source.yml &
PID=$!
sleep 5
kill -SIGINT $PID
wait $PID
echo "Exit code: $?"
# Expected: graceful shutdown, exit code 0
```

### 3.5 — Comparison with lantanav2

Run the exact same config on both binaries and compare:

```bash
# lantanav2
cd /home/vms/Lantana/Dev/lantanav2
timeout 30 ./build/bin/lantana -c configs/deepstream_default.yml 2>&1 | tee /tmp/lantanav2.log

# vms-engine
cd /home/vms/Lantana/Dev/vms-engine
timeout 30 ./build/bin/vms_engine -c configs/deepstream_default.yml 2>&1 | tee /tmp/vms_engine.log

# Compare key log lines (ignoring timestamps and minor formatting)
diff <(grep -E "Configuration loaded|Pipeline.*initialized|Pipeline started|Pipeline.*state" /tmp/lantanav2.log) \
     <(grep -E "Configuration loaded|Pipeline.*initialized|Pipeline started|Pipeline.*state" /tmp/vms_engine.log)
```

---

## Phase 4: Regression Checklist

### Feature Parity Matrix

| Feature                    | lantanav2 | vms-engine | How to Test                              |
| -------------------------- | --------- | ---------- | ---------------------------------------- |
| RTSP source ingestion      | ✅        | ☐          | RTSP stream → fakesink config            |
| File source ingestion      | ✅        | ☐          | MP4 file → fakesink config               |
| Multi-source muxing        | ✅        | ☐          | 2+ sources in config                     |
| Primary inference (nvinfer)| ✅        | ☐          | PGIE with model config                   |
| Secondary inference        | ✅        | ☐          | SGIE chained after PGIE                  |
| Object tracking            | ✅        | ☐          | Tracker in processing_flow               |
| OSD (display overlay)      | ✅        | ☐          | OSD enabled in visuals config            |
| Tiler (multi-view grid)    | ✅        | ☐          | Tiler with batch_size > 1               |
| RTSP output sink           | ✅        | ☐          | RTSP sink in outputs                     |
| File output sink           | ✅        | ☐          | File sink in outputs                     |
| Smart recording            | ✅        | ☐          | Smart record config enabled              |
| Event handling (crop+save) | ✅        | ☐          | Custom handler with storage              |
| Redis stream publishing    | ✅        | ☐          | MQ publisher enabled + Redis running     |
| REST API control           | ✅        | ☐          | REST API enabled, curl test              |
| S3 storage upload          | ✅        | ☐          | S3 storage config + event trigger        |
| Local storage save         | ✅        | ☐          | Local storage config + event trigger     |
| Triton inference           | ✅        | ☐          | External service config                  |
| Analytics (nvdsanalytics)  | ✅        | ☐          | Analytics config in processing_flow      |
| Graceful shutdown (SIGINT) | ✅        | ☐          | kill -SIGINT PID                         |
| Crash recovery (SIGSEGV)   | ✅        | ☐          | Deliberate segfault test                 |
| Runtime param update       | ✅        | ☐          | REST API endpoint                        |
| Runtime stream add/remove  | ✅        | ☐          | REST API endpoint                        |

---

## Phase 5: CI Pipeline Setup (Optional)

### Dockerfile.ci

```dockerfile
FROM nvcr.io/nvidia/deepstream:7.1-gc-triton-devel

# Install build dependencies
RUN apt-get update && apt-get install -y \
    cmake build-essential pkg-config \
    libglib2.0-dev libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . .

RUN cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream \
    && cmake --build build -- -j$(nproc)

# Run static checks
RUN grep -rn "lantana" core/ pipeline/ domain/ infrastructure/ services/ app/ \
    --include="*.hpp" --include="*.cpp" \
    | grep -v "// lantana" | wc -l | grep -q "^0$"
```

### GitHub Actions (if applicable)

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
          cmake -S . -B build -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream
          cmake --build build -- -j$(nproc)
      - name: Static Checks
        run: |
          ! grep -rn "lantana" core/ pipeline/ domain/ infrastructure/ services/ app/ --include="*.hpp" --include="*.cpp" | grep -v "//"
          ! grep -rn "LANTANA_WITH_" --include="*.hpp" --include="*.cpp" --include="CMakeLists.txt"
```

---

## Final Completion Checklist

- [ ] Phase 1: All 6 static analysis checks pass (0 violations)
- [ ] Phase 2: Clean build succeeds, all targets compile, binary runs
- [ ] Phase 3.1: Argument parsing works (no crash)
- [ ] Phase 3.2: Config load works (logs summary)
- [ ] Phase 3.3: Single source pipeline runs (frames processed)
- [ ] Phase 3.4: Signal handling works (graceful shutdown)
- [ ] Phase 3.5: Output matches lantanav2 for same config
- [ ] Phase 4: All 20+ features checked in regression matrix
- [ ] Layer dependency rule enforced (core → ∅, domain → core only)
- [ ] No `ds_` prefixed files or classes remain
- [ ] No `_v2` suffixed files or classes remain
- [ ] README.md updated with new build instructions
- [ ] Configs copied/updated to `vms-engine/configs/`
