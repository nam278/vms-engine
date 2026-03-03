# AGENTS.md

## Project Overview

**VMS Engine** — GPU-accelerated video analytics engine built on **NVIDIA DeepStream SDK 8.0** with **C++17**. Processes multi-stream RTSP/URI video with AI inference (TensorRT / Triton), object tracking, analytics, and outputs to display, RTSP, file, and message brokers (Redis/Kafka).

Architecture: **Clean Architecture + Builder Pattern**. Pipeline topology is 100% YAML-driven — no code changes for new deployments.

Key technologies: C++17 · CMake 3.16+ · GStreamer 1.0 · NVIDIA DeepStream 8.0 · TensorRT · spdlog · yaml-cpp · Redis/Kafka · Pistache HTTP.

Root namespace: `engine::` (maps to include prefix `engine/`).

---

## Container Setup

There are **two Docker images** with distinct purposes:

| Image      | Dockerfile         | Tag                     | Purpose                                           |
| ---------- | ------------------ | ----------------------- | ------------------------------------------------- |
| Dev image  | `Dockerfile`       | `vms-engine-dev:latest` | Full build tools, GDB, Valgrind — for development |
| Prod image | `Dockerfile.image` | `vms-engine:latest`     | Lightweight runtime — binary baked in             |

**Build the dev image:**

```bash
docker build -t vms-engine-dev:latest .
```

**Start dev container:**

```bash
# Copy and fill env vars first:
cp .env.example .env  # set APP_UID, APP_GID

docker compose up -d
docker compose exec app bash
```

The container mounts `.:/opt/vms_engine` — all source edits on the host are reflected immediately inside the container.

---

## Build Commands

> 📖 **Full CMake Reference** → [`docs/architecture/CMAKE.md`](docs/architecture/CMAKE.md)  
> Bao gồm: build types, FetchContent, CMake Presets, generator expressions, custom targets, anti-patterns.

> 📖 **DeepStream Architecture Docs** → [`docs/architecture/deepstream/README.md`](docs/architecture/deepstream/README.md)  
> 11 files covering: pipeline building, linking, config system, runtime lifecycle, event handlers/probes, analytics, outputs, smart record, signal vs probe.

**Dev workflow**: Source code is edited on the host (mounted into container at `/opt/vms_engine`). Only these commands need to run **inside the container**: cmake configure, cmake build, and running the binary.

All build commands run **inside the dev container** at `/opt/vms_engine`.

```bash
# Configure (Debug — default for development)
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream \
    -G Ninja

# Build (parallel)
cmake --build build -- -j5

# Run
./build/bin/vms_engine -c configs/default.yml

# Release build
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream \
    -G Ninja
cmake --build build -- -j5

# Clean rebuild
rm -rf build && cmake -S . -B build ... && cmake --build build -- -j5
```

**`DEEPSTREAM_DIR`** is automatically set to `/opt/nvidia/deepstream/deepstream` inside the container via the `ENV` in Dockerfile. Can be overridden.

**Build output:**

- Executable: `build/bin/vms_engine`
- Libraries: `build/lib/libvms_engine_*.a`
- Compile DB: `build/compile_commands.json` (used by clangd)

---

## Development Workflow

Typical development cycle inside the container:

```bash
# 1. Edit source on HOST (IDE/editor) — changes appear immediately in container via mount
# 2. Enter container and rebuild
cmake --build build -- -j5

# 3. Run with a config
./build/bin/vms_engine -c configs/default.yml

# 4. Debug with GDB
gdb --args ./build/bin/vms_engine -c configs/default.yml
```

**GStreamer debug logging:**

```bash
GST_DEBUG=3 ./build/bin/vms_engine -c configs/default.yml
GST_DEBUG=nvmultiurisrcbin:5,nvinfer:4 ./build/bin/vms_engine -c configs/default.yml
```

**Runtime data** lands in `dev/` (git-ignored, only `dev/.gitkeep` is tracked):

- Logs: `dev/logs/`
- Smart record clips: `dev/rec/`
- Object crops: `dev/rec/objects/`
- Generated configs: `dev/config/`

---

## Project Structure

```
vms-engine/
├── app/               # Entry point — main.cpp, argument parsing, wiring
├── core/              # Interfaces (ports) + config types + utils — NO external deps
│   ├── include/engine/core/builders/       # IPipelineBuilder, IBuilderFactory, IElementBuilder
│   ├── include/engine/core/config/         # PipelineConfig + all sub-config types
│   ├── include/engine/core/pipeline/       # IPipelineManager
│   ├── include/engine/core/eventing/       # IEventHandler, IEventManager
│   ├── include/engine/core/messaging/      # IMessageProducer, IMessageConsumer
│   ├── include/engine/core/storage/        # IStorageManager
│   └── include/engine/core/utils/          # logger.hpp (LOG_* macros), uuid, thread queue
├── pipeline/          # DeepStream builder implementations
│   ├── include/engine/pipeline/block_builders/  # Phase builders (Source/Processing/Visuals/Outputs)
│   ├── include/engine/pipeline/builders/        # Element builders (per GstElement type)
│   ├── include/engine/pipeline/probes/          # GStreamer pad probe implementations
│   └── include/engine/pipeline/event_handlers/  # Signal-based event handlers
├── domain/            # Business rules — metadata parsing, event filtering, runtime params
├── infrastructure/    # Adapters — YAML parser, Redis/Kafka, S3/local storage, REST API
├── services/          # External clients — Triton Inference Server
├── plugins/           # Runtime-loadable .so plugin handlers
├── configs/           # YAML pipeline config files
├── docs/              # Architecture Blueprint + implementation plans
│   ├── architecture/deepstream/  # Detailed DeepStream architecture docs (11 files)
│   ├── architecture/RAII.md      # RAII guide for GStreamer/CUDA resources
│   ├── architecture/CMAKE.md     # Build system reference
│   └── configs/                  # Annotated YAML config examples
└── dev/               # Runtime data dir (git-ignored)
```

---

## Architecture Rules

These rules are **strictly enforced**. Violations break the architecture:

1. **Dependency Rule** — dependencies point inward only:
   - `core/` → no external dependencies (only std + GStreamer forward-declares)
   - `pipeline/` → depends on `core/` only
   - `domain/` → depends on `core/` only
   - `infrastructure/` → depends on `core/` only
   - `services/` → depends on `core/` only
   - `app/` → may depend on all layers

2. **Interface-first** — define interface in `core/` before implementing in other layers. Never skip the interface step.

3. **Config types in core** — all `*Config` structs live in `core/include/engine/core/config/`. Builders receive `const engine::core::config::PipelineConfig&`, not individual slices.

4. **No `std::variant` wrappers** — vms-engine is DeepStream-native. Config structs reference DeepStream properties directly (no multi-backend variants from lantanav2).

5. **`engine::` namespace everywhere** — never use `lantana::` (old name). Namespace follows directory: `engine::core::pipeline::IPipelineManager`.

---

## Naming Conventions

| Element     | Convention                   | Example                             |
| ----------- | ---------------------------- | ----------------------------------- |
| Namespaces  | `snake_case`                 | `engine::core::pipeline`            |
| Classes     | `PascalCase`                 | `PipelineManager`, `BuilderFactory` |
| Interfaces  | `IPascalCase`                | `IPipelineManager`, `IHandler`      |
| Methods     | `snake_case()`               | `build_pipeline()`, `get_state()`   |
| Member vars | `snake_case_` (trailing `_`) | `pipeline_`, `config_`, `tails_`    |
| Constants   | `UPPER_SNAKE_CASE`           | `DEFAULT_MBROKER_PORT`              |
| Enum values | `PascalCase`                 | `PipelineState::Playing`            |
| Files       | `snake_case.hpp` / `.cpp`    | `pipeline_manager.hpp`              |
| Config IDs  | `snake_case` in YAML         | `"pgie_detector"`, `"muxer_main"`   |

---

## YAML Config Conventions

- **Property names** in YAML use `snake_case` (the YAML parser maps `_` → `-` for GStreamer internally)
- GStreamer docs list properties with hyphens (e.g., `ll-lib-file`) → write as `ll_lib_file` in YAML
- All `enum` fields (e.g., `mode`, `smart_record`, `process_mode`, `compute_hw`) are **integers** in YAML, not strings
- Reference config: `docs/configs/deepstream_default.yml`

```yaml
# Example: correct YAML property conventions
queue_defaults:
  max_size_buffers: 10
  max_size_bytes_mb: 20
  max_size_time_sec: 0.5
  leaky: 2 # 0=none, 1=upstream, 2=downstream
  silent: true

sources:
  type: nvmultiurisrcbin
  max_batch_size: 4
  gpu_id: 0
  width: 1920
  height: 1080
  cameras:
    - name: camera-01
      uri: "rtsp://192.168.1.100:554/stream"
  smart_record: 2 # int: 0=disabled, 1=cloud-only, 2=multi
  smart_rec_dir_path: "/opt/engine/data/rec"
  output_queue:
    max_size_buffers: 5
    leaky: 2

processing:
  elements:
    - id: pgie_detection
      type: nvinfer
      role: primary_inference
      config_file: "/opt/engine/data/components/pgie/config.yml"
      process_mode: 1 # int: 1=primary, 2=secondary
      batch_size: 4
      queue: {} # insert queue with defaults
    - id: tracker
      type: nvtracker
      ll_lib_file: "/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so"
      queue: {}
  output_queue: {}

event_handlers:
  - id: smart_record
    enable: true
    type: on_detect
    probe_element: tracker
    trigger: smart_record
    label_filter: [car, person, truck]
```

---

## DeepStream Element Properties — Quick Reference

YAML uses `snake_case`; GStreamer docs use `kebab-case` — they map to the same property. Canonical config: `docs/configs/deepstream_default.yml`.

### nvmultiurisrcbin (primary source element)

Properties in 3 groups:

**Group 1 — nvmultiurisrcbin direct:**

| YAML key         | GStreamer        | Type   | Notes                                          |
| ---------------- | ---------------- | ------ | ---------------------------------------------- |
| `ip_address`     | `ip-address`     | string | REST API bind address (`localhost`, `0.0.0.0`) |
| `port`           | `port`           | int    | REST API port; **0** = disable                 |
| `max_batch_size` | `max-batch-size` | int    | max number of streams to mux (NOT batch_size)  |
| `mode`           | `mode`           | int    | **0**=video-only, **1**=audio-only             |

**Group 2 — per-source nvurisrcbin pass-through:**

| YAML key                  | GStreamer                 | Type | Notes                                              |
| ------------------------- | ------------------------- | ---- | -------------------------------------------------- |
| `gpu_id`                  | `gpu-id`                  | int  | GPU device ID                                      |
| `num_extra_surfaces`      | `num-extra-surfaces`      | int  | extra decoder surfaces for stability               |
| `cudadec_memtype`         | `cudadec-memtype`         | int  | **0**=device, **1**=pinned, **2**=unified          |
| `dec_skip_frames`         | `dec-skip-frames`         | int  | **0**=all, **1**=non-ref, **2**=key-only           |
| `drop_frame_interval`     | `drop-frame-interval`     | int  | drop every N frames at source (0=none)             |
| `select_rtp_protocol`     | `select-rtp-protocol`     | int  | **0**=multi(UDP+TCP), **4**=TCP-only (recommended) |
| `rtsp_reconnect_interval` | `rtsp-reconnect-interval` | int  | seconds between reconnects (0=disable)             |
| `rtsp_reconnect_attempts` | `rtsp-reconnect-attempts` | int  | reconnect attempts (-1=infinite)                   |
| `latency`                 | `latency`                 | int  | RTSP jitter buffer latency (ms)                    |
| `udp_buffer_size`         | `udp-buffer-size`         | int  | UDP receive buffer bytes (default 524288)          |
| `disable_audio`           | `disable-audio`           | bool | true = skip audio decode                           |
| `disable_passthrough`     | `disable-passthrough`     | bool | disable passthrough mode                           |
| `drop_pipeline_eos`       | `drop-pipeline-eos`       | bool | prevent single-source EOS from killing pipeline    |

**Group 3 — nvstreammux passthrough:**

| YAML key               | GStreamer              | Type | Notes                                    |
| ---------------------- | ---------------------- | ---- | ---------------------------------------- |
| `width`                | `width`                | int  | muxer output width                       |
| `height`               | `height`               | int  | muxer output height                      |
| `batched_push_timeout` | `batched-push-timeout` | int  | µs to wait for full batch (40000 = 40ms) |
| `live_source`          | `live-source`          | bool | true for RTSP                            |
| `sync_inputs`          | `sync-inputs`          | bool | sync timestamps across sources           |

**Smart Record properties (on nvmultiurisrcbin):**

| YAML key                     | GStreamer                    | Type   | Notes                                       |
| ---------------------------- | ---------------------------- | ------ | ------------------------------------------- |
| `smart_record`               | `smart-record`               | int    | **0**=off, **1**=cloud-only, **2**=multi    |
| `smart_rec_dir_path`         | `smart-rec-dir-path`         | string | output directory for recordings             |
| `smart_rec_file_prefix`      | `smart-rec-file-prefix`      | string | filename prefix                             |
| `smart_rec_cache`            | `smart-rec-cache`            | int    | pre-event circular buffer (sec)             |
| `smart_rec_default_duration` | `smart-rec-default-duration` | int    | post-event recording length (sec)           |
| `smart_rec_mode`             | `smart-rec-mode`             | int    | **0**=audio+video, **1**=video, **2**=audio |
| `smart_rec_container`        | `smart-rec-container`        | int    | **0**=mp4, **1**=mkv                        |

### nvinfer (TensorRT inference)

| YAML key               | GStreamer              | Type   | Notes                                            |
| ---------------------- | ---------------------- | ------ | ------------------------------------------------ |
| `config_file`          | `config-file-path`     | string | points to nvinfer YAML/txt config                |
| `role`                 | _(vms-engine only)_    | string | `primary_inference` / `secondary_inference`      |
| `unique_id`            | `gie-unique-id`        | int    | unique ID for this infer element                 |
| `process_mode`         | `process-mode`         | int    | **1**=PGIE (full frame), **2**=SGIE (per-object) |
| `batch_size`           | `batch-size`           | int    | must match muxer batch                           |
| `interval`             | `interval`             | int    | skip N batches between inferences (0=every)      |
| `gpu_id`               | `gpu-id`               | int    |                                                  |
| `operate_on_gie_id`    | `operate-on-gie-id`    | int    | SGIE only — PGIE's unique-id                     |
| `operate_on_class_ids` | `operate-on-class-ids` | string | SGIE — colon-separated class ids                 |

nvinfer `.txt` config key fields: `network-type` (0=Detector, 1=Classifier, 2=Segmentation), `gie-unique-id`, `model-engine-file`, `labelfile-path`, `batch-size`, `input-dims`.

### nvtracker

| YAML key              | GStreamer             | Type   | Notes                                |
| --------------------- | --------------------- | ------ | ------------------------------------ |
| `ll_lib_file`         | `ll-lib-file`         | string | tracker `.so` path                   |
| `ll_config_file`      | `ll-config-file`      | string | tracker YAML config                  |
| `tracker_width`       | `tracker-width`       | int    | processing resolution                |
| `tracker_height`      | `tracker-height`      | int    | processing resolution                |
| `gpu_id`              | `gpu-id`              | int    |                                      |
| `compute_hw`          | `compute-hw`          | int    | **0**=default, **1**=GPU, **2**=VIC  |
| `display_tracking_id` | `display-tracking-id` | bool   | show IDs on OSD                      |
| `user_meta_pool_size` | `user-meta-pool-size` | int    | buffer pool for tracker extra output |

Tracker `.so` files in `/opt/nvidia/deepstream/deepstream/lib/`:

- `libnvds_nvmultiobjecttracker.so` — NvDCF/IOU/NvDeepSORT (recommended)

### nvdsosd (on-screen display)

| YAML key       | GStreamer      | Type | Notes                              |
| -------------- | -------------- | ---- | ---------------------------------- |
| `process_mode` | `process-mode` | int  | **0**=CPU, **1**=GPU, **2**=HW/VIC |
| `display_bbox` | `display-bbox` | bool | draw bounding boxes                |
| `display_text` | `display-text` | bool | draw labels                        |
| `display_mask` | `display-mask` | bool | draw segmentation masks            |
| `border_width` | `border-width` | int  | bbox line thickness                |
| `gpu_id`       | `gpu-id`       | int  |                                    |

### nvmultistreamtiler

| YAML key  | GStreamer | Type | Notes               |
| --------- | --------- | ---- | ------------------- |
| `rows`    | `rows`    | int  | tiling grid rows    |
| `columns` | `columns` | int  | tiling grid columns |
| `width`   | `width`   | int  | output width        |
| `height`  | `height`  | int  | output height       |
| `gpu_id`  | `gpu-id`  | int  |                     |

### nvdsanalytics

| YAML key                 | GStreamer                | Type   | Notes                 |
| ------------------------ | ------------------------ | ------ | --------------------- |
| `config_file`            | `config-file`            | string | analytics config file |
| `gpu_id`                 | `gpu-id`                 | int    |                       |
| `enable_secondary_input` | `enable-secondary-input` | bool   | use SGIE metadata     |

### nvmsgconv + nvmsgbroker (event publishing)

**nvmsgconv:**

| YAML key       | GStreamer      | Type   | Notes                           |
| -------------- | -------------- | ------ | ------------------------------- |
| `config`       | `config`       | string | schema config file              |
| `payload_type` | `payload-type` | int    | **0**=DEEPSTREAM, **1**=MINIMAL |
| `comp_id`      | `comp-id`      | int    | component identifier            |

**nvmsgbroker:**

| YAML key    | GStreamer   | Type   | Notes                    |
| ----------- | ----------- | ------ | ------------------------ |
| `proto_lib` | `proto-lib` | string | adapter `.so` path       |
| `conn_str`  | `conn-str`  | string | broker connection string |
| `topic`     | `topic`     | string | message topic            |
| `config`    | `config`    | string | broker config file       |
| `sync`      | `sync`      | bool   | synchronous publish      |

Protocol adapters in `/opt/nvidia/deepstream/deepstream/lib/`:

- `libnvds_kafka_proto.so` — Kafka
- `libnvds_redis_proto.so` — Redis
- `libnvds_amqp_proto.so` — AMQP / RabbitMQ

### nvv4l2h264enc / nvv4l2h265enc (hardware encoder)

| YAML key         | GStreamer        | Type | Notes                                |
| ---------------- | ---------------- | ---- | ------------------------------------ |
| `bitrate`        | `bitrate`        | int  | bps (e.g. 4000000 = 4 Mbps)          |
| `iframeinterval` | `iframeinterval` | int  | keyframe interval                    |
| `preset_level`   | `preset-level`   | int  | **1**=UltraFast … **4**=Medium       |
| `insert_sps_pps` | `insert-sps-pps` | bool | **true** required for RTSP streaming |
| `maxperf_enable` | `maxperf-enable` | bool | max hardware throughput              |

### NvDs Metadata structs (in pad probes)

```
NvDsBatchMeta           → gst_buffer_get_nvds_batch_meta(buffer)
  └── NvDsFrameMeta     → frame_meta_list
        └── NvDsObjectMeta → obj_meta_list
              ├── class_id, confidence, tracker_id
              ├── rect_params (NvOSD_RectParams: left/top/width/height)
              └── NvDsClassifierMeta → classifier_meta_list (SGIE results)
NvDsEventMsgMeta        → attached by event handlers for nvmsgconv
```

---

## Logging

Use the `LOG_*` macros from `engine/core/utils/logger.hpp`. Never use `std::cout` or `printf` in production code.

```cpp
#include "engine/core/utils/logger.hpp"

LOG_T("Trace: detailed debug info");
LOG_D("Debug: building element: {}", element_name);
LOG_I("Info: Pipeline started with {} sources", source_count);
LOG_W("Warning: deprecated config field '{}' used", field_name);
LOG_E("Error: Failed to create element: {}", name);
LOG_C("Critical: Pipeline initialization failed");
```

---

## Code Style

- **Standard**: C++17 — use structured bindings, `std::optional`, `std::string_view`, `if constexpr` where appropriate
- **Headers**: Use `#pragma once` (no include guards)
- **Includes**: Prefer `<engine/core/...>` angle-bracket style for project headers
- **Smart pointers**: Use `std::unique_ptr` for owned resources, `std::shared_ptr` when shared ownership is needed
- **No raw owning pointers**: raw `T*` is acceptable for non-owning observer/borrowed references only
- **GStreamer resources**: wrap in `std::unique_ptr` with custom deleters or RAII wrappers
- **Constructors**: prefer constructor injection for dependencies; avoid global state
- **Error handling**: return `bool` for build/init operations with `LOG_E` before returning false; throw only in constructors when unrecoverable

## Doxygen Comment Standard

All public interfaces and non-trivial implementations must use Doxygen-style comments.

- **Public class/interface**: use `/** ... */` with `@brief` and short behavioral contract.
- **Public methods/functions**: document parameters (`@param`), return (`@return`), and side effects.
- **Enums/constants**: add one-line `/** @brief ... */` explaining runtime semantics.
- **Avoid noise**: do not restate obvious code; document intent, invariants, ownership, lifecycle.

```cpp
/**
 * @brief Builds and wires the DeepStream pipeline from validated runtime config.
 * @param config Full immutable pipeline configuration object.
 * @param loop Main loop used by async bus watch and signal callbacks.
 * @return true when build completed and all mandatory links succeeded.
 */
virtual bool build(const engine::core::config::PipelineConfig& config,
                   GMainLoop* loop) = 0;
```

```cpp
/** @brief Runtime event key for PGIE object detections. */
inline constexpr std::string_view ON_DETECT = "on_detect";
```

---

## Memory Management & RAII

> **Full RAII guide → [`docs/architecture/RAII.md`](docs/architecture/RAII.md)**  
> Covers: heap, file handles, sockets, mutex/locks, timers, scope guards,
> GStreamer resources, NvDs rules, custom destructor classes, Rule of Five,
> GPU/CUDA resources, RAII in containers, `[[nodiscard]]`, exception safety.

### GStreamer Ownership Quick Reference

| Object                     | Created via                            | Release via           | Notes                             |
| -------------------------- | -------------------------------------- | --------------------- | --------------------------------- |
| `GstElement*` (not in bin) | `gst_element_factory_make()`           | `gst_object_unref()`  | Caller owns until `gst_bin_add()` |
| `GstElement*` in bin       | `gst_bin_add(bin, elem)`               | _(bin owns)_          | **Do NOT unref after add**        |
| `GstPad*`                  | `gst_element_get_static_pad()`         | `gst_object_unref()`  | Must unref even read-only         |
| `GstCaps*`                 | `gst_caps_new_*()` / `gst_caps_copy()` | `gst_caps_unref()`    |                                   |
| `GstBus*`                  | `gst_pipeline_get_bus()`               | `gst_object_unref()`  |                                   |
| `GMainLoop*`               | `g_main_loop_new()`                    | `g_main_loop_unref()` |                                   |
| `GError*`                  | GStreamer out param                    | `g_error_free()`      |                                   |
| `gchar*`                   | `g_object_get()` / `g_strdup()`        | `g_free()`            |                                   |
| `NvDsBatchMeta*`           | `gst_buffer_get_nvds_batch_meta()`     | **DO NOT FREE**       | pipeline owns                     |
| `NvDsFrameMeta*`           | iterated from `batch_meta`             | **DO NOT FREE**       |                                   |
| `NvDsObjectMeta*`          | iterated from `frame_meta`             | **DO NOT FREE**       |                                   |

### RAII Helpers (`gst_utils.hpp`)

```cpp
#include "engine/core/utils/gst_utils.hpp"

// Element guard — call release() after successful gst_bin_add()
auto elem = engine::core::utils::make_gst_element("nvinfer", id.c_str());
if (!elem) { LOG_E("Failed"); return nullptr; }  // auto-unref
g_object_set(G_OBJECT(elem.get()), "config-file-path", cfg.c_str(), nullptr);
if (!gst_bin_add(GST_BIN(bin_), elem.get())) return nullptr;  // auto-unref
return elem.release();  // bin owns — disarm guard

// Pad guard
engine::core::utils::GstPadPtr pad(
    gst_element_get_static_pad(element, "src"), gst_object_unref);
gst_pad_add_probe(pad.get(), GST_PAD_PROBE_TYPE_BUFFER, my_cb, nullptr, nullptr);
// gst_object_unref called automatically

// String from g_object_get
gchar* raw = nullptr;
g_object_get(G_OBJECT(src), "uri", &raw, nullptr);
engine::core::utils::GCharPtr uri(raw, g_free);
LOG_I("URI: {}", uri.get());  // g_free called automatically
```

**Choosing the right tool:**

- Single-call cleanup → `GstPadPtr`, `GstCapsPtr`, … (`unique_ptr` alias)
- Multi-step cleanup (remove_watch → unref; set_state(NULL) → unref) → custom class with `~Destructor()`
- See `GstBusGuard` and `GstPipelineOwner` in [RAII.md](docs/architecture/RAII.md#9-custom-raii-class-with-destructor)

---

## Adding Common Items

### New Element Builder

1. Create `pipeline/include/engine/pipeline/builders/my_builder.hpp` — implement `IElementBuilder`
2. Create `pipeline/src/builders/my_builder.cpp`
3. Method signature: `GstElement* build(const engine::core::config::PipelineConfig& config, int index = 0)`
4. Register in `pipeline/src/builder_factory.cpp` (map role/type string → class)
5. Add config struct to `core/include/engine/core/config/config_types.hpp` if new config section needed
6. Add YAML parser section in `infrastructure/config_parser/yaml_parser_*.cpp`

### New Event Handler

1. Create `pipeline/include/engine/pipeline/event_handlers/my_handler.hpp` — implement `IEventHandler`
2. Create `pipeline/src/event_handlers/my_handler.cpp`
3. Register in `HandlerManager`
4. Add YAML under `event_handlers:` + update handler config parser if new fields

### New Infrastructure Adapter

1. Define/extend interface in `core/include/engine/core/<subsystem>/i*.hpp`
2. Implement in `infrastructure/<subsystem>/`
3. Wire in `app/main.cpp` via constructor injection

---

## Testing

Currently no automated test suite. Manual validation:

```bash
# Syntax-only build check (catches compile errors)
cmake --build build -- -j5 2>&1 | head -50

# Runtime smoke test
./build/bin/vms_engine -c configs/default.yml

# DOT graph export (visual pipeline inspection)
# Set dot_file_dir in YAML, then:
dot -Tpng dev/logs/pipeline.dot -o pipeline_graph.png
```

---

## Debugging

```bash
# GDB
gdb --args ./build/bin/vms_engine -c configs/default.yml
(gdb) run
(gdb) bt         # backtrace on crash

# Valgrind (memory leak check)
valgrind --leak-check=full ./build/bin/vms_engine -c configs/default.yml

# GStreamer pipeline trace
GST_DEBUG=3 ./build/bin/vms_engine -c configs/default.yml
GST_DEBUG_DUMP_DOT_DIR=dev/logs GST_DEBUG=2 ./build/bin/vms_engine -c configs/default.yml

# Common failure: DEEPSTREAM_DIR not set
echo $DEEPSTREAM_DIR   # should be /opt/nvidia/deepstream/deepstream
```

**Common build errors:**

- `nvdsstreammux.h: No such file` → `DEEPSTREAM_DIR` incorrect or DeepStream not installed
- `glib-2.0 not found` → `pkg-config` not finding GLib; check `pkg-config --list-all | grep glib`
- `yaml-cpp not found` → CMake auto-fetches via FetchContent, check internet access in container

---

## PR / Commit Guidelines

- Title format: `[layer] Brief description` — e.g., `[pipeline] Add nvdsanalytics support to ProcessingBuilder`
- Keep changes layer-scoped where possible
- Do not mix architecture changes with feature additions in one PR
- Run build and verify no warnings before committing: `cmake --build build -- -j5 2>&1 | grep -i warning`

---

## Important: This Is NOT lantanav2

This project is a refactor of `lantanav2`. Do **not** mix up names/paths:

| Item             | lantanav2 (OLD)         | vms-engine (THIS)    |
| ---------------- | ----------------------- | -------------------- |
| Namespace        | `lantana::`             | `engine::`           |
| Include prefix   | `lantana/core/`         | `engine/core/`       |
| Executable       | `lantana`               | `vms_engine`         |
| Backend dir      | `backends/deepstream/`  | `pipeline/`          |
| Element builders | `ds_source_builder.hpp` | `source_builder.hpp` |
