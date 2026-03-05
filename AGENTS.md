# AGENTS.md

## Project Overview

VMS Engine is a GPU-accelerated video analytics engine built with DeepStream 8.0 and C++17.
It processes multi-stream RTSP/file inputs, runs AI inference, tracking, and analytics, and emits outputs to RTSP, files, and message brokers.

Core stack:

- C++17
- CMake 3.16+
- NVIDIA DeepStream 8.0
- GStreamer 1.14+
- TensorRT / Triton integration points
- spdlog, yaml-cpp, hiredis, nlohmann/json, librdkafka

Architecture style:

- Clean architecture boundaries
- Interface-first design (`core/` defines contracts)
- Config-driven pipelines (YAML)

Root namespace and include prefix:

- Namespace: `engine::`
- Include root: `engine/...`

## Repository Layout

```text
vms-engine/
  app/               # executable entrypoint (main.cpp)
  core/              # interfaces, config types, shared utils
  pipeline/          # DeepStream/GStreamer pipeline builders and probes
  domain/            # domain rules and policy logic
  infrastructure/    # config parser, messaging, storage, rest adapters
  plugins/           # custom DeepStream parser .so plugins
  configs/           # runtime YAML configs
  docs/              # architecture and plans
  scripts/           # formatting and git hook scripts
```

## Setup Commands

Run from repository root unless noted.

### Container setup (recommended)

```bash
# one-time env for UID/GID mapping
cp .env.example .env

# build dev image
docker build -t vms-engine-dev:latest .

# start container and attach shell
docker compose up -d
docker compose exec app bash
```

Inside the dev container, source is mounted at `/opt/vms_engine`.

### Local prerequisites (non-container)

- DeepStream SDK at `/opt/nvidia/deepstream/deepstream` (or set custom `DEEPSTREAM_DIR`)
- CUDA toolkit and GStreamer dev packages
- `cmake`, `ninja`, `pkg-config`, `clang-format`

## Development Workflow

### Configure

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream \
  -G Ninja
```

### Build

```bash
cmake --build build -- -j5
```

### Build specific targets

```bash
cmake --build build --target vms_engine_core -- -j5
cmake --build build --target vms_engine_domain -- -j5
cmake --build build --target vms_engine_pipeline -- -j5
cmake --build build --target vms_engine_config_parser -- -j5
cmake --build build --target vms_engine_messaging -- -j5
cmake --build build --target vms_engine_storage -- -j5
cmake --build build --target vms_engine_rest_api -- -j5
cmake --build build --target vms_engine -- -j5
```

### Run

```bash
./build/bin/vms_engine -c configs/default.yml
```

If running outside container and libraries are not found:

```bash
export LD_LIBRARY_PATH=/opt/nvidia/deepstream/deepstream/lib:/usr/local/cuda/lib64:$LD_LIBRARY_PATH
```

### Clean rebuild

```bash
rm -rf build
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream \
  -G Ninja
cmake --build build -- -j5
```

## Testing Instructions

There is currently no committed unit/integration test suite (`ctest`/`add_test` not configured).
Validation is done via build checks, static checks, and runtime smoke tests.

### 1. Static checks (required)

```bash
# no legacy namespace/include remnants
grep -REn --include='*.hpp' --include='*.cpp' '\\blantana::|"lantana/' \
  core/ pipeline/ domain/ infrastructure/ app/

# no legacy backend compile guards
grep -rn "LANTANA_WITH_" core/ pipeline/ domain/ infrastructure/ app/ --include="*.hpp" --include="*.cpp"

# no DLStreamer references in engine code
grep -rn "dlstreamer\|DLStreamer\|DLSTREAMER" core/ pipeline/ domain/ infrastructure/ app/ --include="*.hpp" --include="*.cpp"
```

Expected result: no matches.

### 2. Build verification (required)

```bash
cmake --build build -- -j5
file build/bin/vms_engine
ldd build/bin/vms_engine | grep -i "not found"
```

Expected result: build succeeds, executable exists, no missing shared libraries.

### 3. Runtime smoke tests (required for runtime changes)

```bash
# help/argument parsing should not crash
./build/bin/vms_engine --help

# default config startup
timeout 30 ./build/bin/vms_engine -c configs/default.yml
```

For detailed phase checklist, use:

- `docs/plans/phase1_refactor/07_integration_testing.md`

## Code Style Guidelines

### Language and API constraints

- Use C++17 only. Do not introduce C++20/23 features.
- Target DeepStream SDK 8.0 APIs.
- Prefer existing codebase patterns over inventing new abstractions.

### Architecture constraints

- `core/`: no dependency on pipeline/domain/infrastructure implementations.
- `domain/`: depends on `core/` only.
- `pipeline/`: depends on `core/` only.
- `infrastructure/`: depends on `core/` only.
- Define/extend interface in `core/` before implementing elsewhere.
- Builders should consume full `PipelineConfig`, not ad-hoc config slices.

### Naming and structure

- Namespace: `engine::...` only.
- File names: `snake_case`.
- Class names: `PascalCase`; interfaces prefixed with `I`.
- Methods and variables: `snake_case`.

### Logging and comments

- Use logger macros/utilities from `core/utils/logger.hpp`.
- Avoid `std::cout` for runtime diagnostics in library code.
- Use Doxygen comments on public interfaces and non-obvious behavior.

### Formatting

```bash
# format all C/C++ files
./scripts/format.sh

# check formatting only (non-zero exit if changes needed)
./scripts/format.sh --check

# CMake formatting target (if clang-format exists)
cmake --build build --target vms_format
```

Optional git hook installation:

```bash
./scripts/install-hooks.sh
```

## Build and Deployment

### Release build

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream \
  -G Ninja
cmake --build build -- -j5
```

### Production image

```bash
docker build -f Dockerfile.image -t vms-engine:latest .
```

Notes:

- Executable target is `build/bin/vms_engine`.
- `app/CMakeLists.txt` copies `configs/` to `build/bin/configs/` after build.
- Plugins are built to `build/bin/plugins/`.

## Security Considerations

- Do not commit secrets, credentials, or private endpoints in configs.
- Keep RTSP credentials and broker secrets out of tracked YAML where possible.
- Validate external input paths/URIs before passing into pipeline config.
- Prefer least-privilege access for mounted host directories and container runtime.

## Pull Request Guidelines

Use small, layer-scoped changes.

Recommended PR title format:

- `[core] ...`
- `[pipeline] ...`
- `[domain] ...`
- `[infrastructure] ...`
- `[app] ...`

Before opening PR, run at minimum:

```bash
./scripts/format.sh --check
cmake --build build -- -j5
```

For runtime-impact changes, also run smoke test:

```bash
timeout 30 ./build/bin/vms_engine -c configs/default.yml
```

## Debugging and Troubleshooting

### Common commands

```bash
# run with higher GStreamer verbosity
GST_DEBUG=3 ./build/bin/vms_engine -c configs/default.yml

# focused gst debug categories
GST_DEBUG=nvmultiurisrcbin:5,nvinfer:4 ./build/bin/vms_engine -c configs/default.yml

# gdb
gdb --args ./build/bin/vms_engine -c configs/default.yml

# valgrind (CPU-side issues)
valgrind --leak-check=full ./build/bin/vms_engine -c configs/default.yml
```

### Frequent issues

- Missing DeepStream headers/libs:
  - Check `DEEPSTREAM_DIR` and SDK installation under `/opt/nvidia/deepstream/deepstream`.
- `nv*` GStreamer elements not found:
  - Ensure DeepStream runtime environment is correctly initialized in container.
- Runtime link errors:
  - Verify `LD_LIBRARY_PATH` includes DeepStream and CUDA libs when outside container.
- Crash in pipeline startup:
  - Re-run with `GST_DEBUG=3` and inspect logs in `build/bin/logs/` or `dev/logs/`.

## Agent Scope Notes

- This repository sits in a larger multi-root workspace; avoid cross-repo edits unless explicitly requested.
- Keep AGENTS instructions aligned with real, runnable commands from this repo.
- Update this file when build/test workflow or architecture contracts change.
