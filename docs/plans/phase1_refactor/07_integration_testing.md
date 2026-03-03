# Plan 07 — Integration Testing & Verification

> End-to-end verification that the fully-built vms-engine builds, runs, and processes video.
> This plan is executed **after all implementation (Plans 01–06) is complete**.

---

## Prerequisites

- Plans 01–06 completed
- `cmake --build build -- -j5` succeeds (full build)
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

### 1.4 — Zero ds\_ Prefixes (Excluding Plugins & DeepStream SDK Includes)

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
# Inside container: docker compose exec app bash
cd /opt/vms_engine
rm -rf build
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream \
    -G Ninja
cmake --build build -- -j5
```

### 2.2 — Individual Target Build

```bash
cmake --build build --target vms_engine_core -- -j5
cmake --build build --target vms_engine_domain -- -j5
cmake --build build --target vms_engine_pipeline -- -j5
cmake --build build --target vms_engine_config_parser -- -j5
cmake --build build --target vms_engine_messaging -- -j5
cmake --build build --target vms_engine_storage -- -j5
cmake --build build --target vms_engine_rest_api -- -j5
cmake --build build --target vms_engine_services -- -j5
cmake --build build --target vms_engine -- -j5
```

### 2.3 — Plugin Build

```bash
# All 7 plugins should produce .so files
cmake --build build -- -j5
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
# test_minimal.yml
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

```bash
./build/bin/vms_engine -c test_minimal.yml
# Expected: loads config, logs summary, exits cleanly (no cameras = no pipeline start)
# Should see: "Configuration loaded successfully"
```

### 3.3 — Single Source Pipeline Test

Use a config with a single RTSP or file source:

```yaml
# test_single_source.yml
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
    - name: test_cam
      uri: "file:///path/to/test_video.mp4"
  output_queue: {}

processing:
  elements:
    - id: pgie
      type: nvinfer
      role: primary_inference
      config_file: "/opt/engine/data/components/pgie/config.yml"
      process_mode: 1
      batch_size: 1
      queue: {}
  output_queue: {}

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

```bash
timeout 30 ./build/bin/vms_engine -c test_single_source.yml
# Expected: pipeline starts, processes frames for 30s, exits
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

---

## Phase 4: Feature Checklist

### Feature Status Matrix

| Feature                     | Status | How to Test                             |
| --------------------------- | ------ | --------------------------------------- |
| RTSP source ingestion       | ☐      | RTSP stream → fakesink config           |
| File source ingestion       | ☐      | MP4 file → fakesink config              |
| Multi-source muxing         | ☐      | 2+ cameras in config                    |
| Primary inference (nvinfer) | ☐      | PGIE with model config                  |
| Secondary inference         | ☐      | SGIE chained after PGIE                 |
| Object tracking             | ☐      | nvtracker in processing elements        |
| OSD (display overlay)       | ☐      | nvdsosd enabled in visuals block        |
| Tiler (multi-view grid)     | ☐      | nvmultistreamtiler with batch_size > 1  |
| RTSP output sink            | ☐      | rtspclientsink in outputs               |
| File output sink            | ☐      | filesink in outputs                     |
| Smart recording             | ☐      | `smart_record: 2` in sources config     |
| Event handling (crop+save)  | ☐      | Custom handler with storage             |
| Redis stream publishing     | ☐      | Messaging config + Redis running        |
| REST API control            | ☐      | REST API enabled, curl test             |
| S3 storage upload           | ☐      | S3 storage config + event trigger       |
| Local storage save          | ☐      | Local storage config + event trigger    |
| Triton inference            | ☐      | External service config                 |
| Analytics (nvdsanalytics)   | ☐      | Analytics config in processing elements |
| Graceful shutdown (SIGINT)  | ☐      | `kill -SIGINT PID`                      |
| Crash recovery (SIGSEGV)    | ☐      | Deliberate segfault test                |
| Runtime param update        | ☐      | REST API endpoint                       |
| Runtime stream add/remove   | ☐      | REST API endpoint                       |

---

## Phase 5: CI Pipeline Setup (Optional)

### Dockerfile.ci

```dockerfile
FROM nvcr.io/nvidia/deepstream:8.0-gc-triton-devel

# Install build dependencies
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

# Run static checks
RUN ! grep -rn "lantana" core/ pipeline/ domain/ infrastructure/ services/ app/ \
    --include="*.hpp" --include="*.cpp" | grep -v "// lantana"
```

### GitHub Actions (if applicable)

```yaml
# .github/workflows/build.yml
name: Build & Verify
on: [push, pull_request]
jobs:
  build:
    runs-on: self-hosted # GPU runner with DeepStream SDK
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
- [ ] Phase 4: All 22 features checked in feature matrix
- [ ] Layer dependency rule enforced (core → ∅, domain → core only)
- [ ] No `ds_` prefixed files or classes in engine code
- [ ] `engine::` namespace used throughout all layers
- [ ] configs/ directory has `default.yml` and example configs
- [ ] `docs/configs/deepstream_default.yml` matches implemented parser schema
