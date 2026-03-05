# VMS Engine

> GPU-accelerated video analytics engine — **NVIDIA DeepStream 8.0** · **C++17** · **Config-Driven**

VMS Engine processes multi-stream RTSP/file video sources with AI inference, object tracking, and analytics. Pipelines are fully described by YAML config; no code changes required for new deployments. Outputs go to RTSP streaming, local file recording, and message brokers (Redis Streams / Kafka).

---

## Features

- **GPU-native** — built on NVIDIA DeepStream 8.0, hardware-accelerated decode, inference (TensorRT), and encode via `nvv4l2h264enc`
- **Multi-stream** — simultaneous RTSP/URI sources via `nvmultiurisrcbin`; configurable batch size
- **AI inference** — primary and secondary inference via `nvinfer` (TensorRT) or `nvinferserver` (Triton)
- **Smart Recording** — event-triggered recording with pre-event buffer built into `nvmultiurisrcbin`
- **Object cropping** — pad probe callback saves per-object JPEG crops at configurable intervals
- **Messaging** — publishes detection events to Redis Streams or Kafka
- **Custom plugins** — 7 runtime-loadable `.so` plugins (YOLO variants, OCR plate parsers, person attribute classifiers)
- **Clean architecture** — interface-first design; `core/` defines all contracts; inner layers never import outer layers
- **Observable** — structured logging via spdlog, GStreamer `.dot` graph export on state transitions

---

## Architecture

### Layer dependency

```
app/            → pipeline, infrastructure, domain, core
pipeline/       → core ONLY
infrastructure/ → core ONLY
domain/         → core ONLY
core/           → std + GStreamer forward-declares ONLY
```

> [!IMPORTANT]
> `core/` never includes headers from `pipeline/`, `infrastructure/`, or `domain/`. Violations break the architecture contract.

### Pipeline topology

```
nvmultiurisrcbin  (decode + mux → batched frames)
       │
       ▼  [processing_bin]
  nvinfer (PGIE) → nvtracker → [nvinfer (SGIE, optional)]
       │
       ▼  [visuals_bin]  (optional)
  nvmultistreamtiler → nvdsosd
       │
       ▼  [output_bin_{id}]
  nvvideoconvert → nvv4l2h264enc → h264parse → rtspclientsink
```

Each stage is an independent `GstBin` with ghost pads. `PipelineBuilder` orchestrates 5 build phases and maintains a `tails_` registry linking bins sequentially.

### Repository layout

```
vms-engine/
├── app/               # Executable entry point (main.cpp)
├── core/              # Interfaces, config types, shared RAII utils
├── pipeline/          # GStreamer/DeepStream builders and pad probes
├── domain/            # Domain rules and event processing policy
├── infrastructure/    # Config parser, Redis/Kafka messaging, storage, REST adapter
├── plugins/           # Custom DeepStream parser .so plugins (built alongside binary)
├── configs/           # Reference YAML configs (copied to build/bin/configs/ on build)
├── docs/              # Architecture docs and implementation plans
└── scripts/           # clang-format runner and git hook installer
```

---

## Prerequisites

- NVIDIA GPU with driver **≥ 535**
- **Docker** + [NVIDIA Container Toolkit](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/install-guide.html)
- VS Code with [Dev Containers extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers) (recommended)

| Dockerfile          | Image tag               | Purpose                                                   |
| ------------------- | ----------------------- | --------------------------------------------------------- |
| `Dockerfile`        | `vms-engine-dev:latest` | Development — includes build tools, gdb, valgrind         |
| `Dockerfile.image`  | `vms-engine:latest`     | Production — binary only, lightweight DeepStream base     |

---

## Getting Started

### 1. Create `.env`

```bash
echo "APP_UID=$(id -u)" > .env
echo "APP_GID=$(id -g)" >> .env
```

### 2. Build and start the dev container

```bash
docker build -t vms-engine-dev:latest .
docker compose up -d
```

> [!NOTE]
> The Dockerfile is based on `nvcr.io/nvidia/deepstream:8.0-gc-triton-devel`. The `docker-compose.yml` mounts the full source tree into `/opt/vms_engine` — edits on the host are immediately visible inside the container.

### 3. Attach a shell

```bash
docker compose exec app bash
```

---

## Build

All commands run **inside the container** at `/opt/vms_engine`.

### Configure

```bash
cmake -S . -B build \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream
```

### Compile

```bash
cmake --build build -- -j5
```

> [!TIP]
> Each `cc1plus` process can consume 300–500 MB of RAM during compilation. Use `-j5` to avoid OOM on memory-constrained machines.

Binary output: `build/bin/vms_engine`

```
build/bin/
├── vms_engine            ← self-contained executable
├── configs/              ← copied from configs/ at build time
├── logs/                 ← pre-created log directory
└── plugins/              ← 7 custom DeepStream parser .so files
    ├── libnvdsinfer_custom_impl_Yolo.so
    ├── libnvdsinfer_custom_impl_Yolo_padded.so
    ├── libnvdsinfer_custom_impl_Yolo_face.so
    ├── libocr_fast_plate_parser.so
    ├── libocr_fast_plate_parser_vn.so
    ├── liblantana_person_attr_parser.so
    └── liblantana_person_attr_24label.so
```

### Clean rebuild

```bash
rm -rf build/
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream
cmake --build build -- -j5
```

---

## Running

```bash
# Inside the container at /opt/vms_engine
./build/bin/vms_engine -c dev/configs/my_pipeline.yml
```

### Runtime directory layout (`dev/`)

`dev/` is git-ignored; only `.gitkeep` files are tracked. Populate before running:

```
dev/
├── configs/                  # Your pipeline YAML files
├── models/                   # TensorRT engines, ONNX models, label files
│   └── my-model/
│       ├── labels.txt
│       └── 1/model.onnx
├── pipeline_components/      # Per-component nvinfer configs
├── rec/                      # Smart record MP4 outputs
│   └── objects/              # Object crop JPEGs
├── config/                   # Tracker and misc runtime configs
└── logs/                     # App logs and GStreamer .dot graphs
```

```bash
# Start from the reference config
cp docs/configs/deepstream_default.yml dev/configs/my_test.yml
# Edit URIs, model paths, output targets...
./build/bin/vms_engine -c dev/configs/my_test.yml
```

---

## Configuration

Pass a YAML file with `-c`. Full schema: [`docs/configs/deepstream_default.yml`](docs/configs/deepstream_default.yml)

```yaml
version: "1.0.0"

pipeline:
  id: "de1"
  name: "Intrusion Detection Pipeline"
  log_level: "INFO"           # DEBUG | INFO | WARN | ERROR
  gst_log_level: "*:1"
  dot_file_dir: "/opt/vms_engine/dev/logs"
  log_file: "/opt/vms_engine/dev/logs/app.log"

queue_defaults:
  max_size_buffers: 10
  leaky: 2                    # 0=none  1=upstream  2=downstream (integer, not string)

sources:
  type: nvmultiurisrcbin
  max_batch_size: 4
  mode: 0                     # 0=video  1=audio
  width: 1920
  height: 1080
  cameras:
    - id: camera-01
      uri: rtsp://192.168.1.99:8554/stream
  smart_record: 2             # 0=off  1=cloud  2=multi(cloud+local)
  smart_rec_dir_path: "/opt/vms_engine/dev/rec"

processing:
  elements:
    - id: pgie_detection
      type: nvinfer
      role: primary_inference
      config_file: "/opt/vms_engine/dev/pipeline_components/pgie_detection/config.yml"
      process_mode: 1         # 1=Primary (full-frame)
      batch_size: 4
      queue: {}
    - id: tracker
      type: nvtracker
      ll_lib_file: "/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so"
      ll_config_file: "/opt/vms_engine/dev/config/tracker_NvDCF_perf.yml"
      tracker_width: 640
      tracker_height: 640
      compute_hw: 1           # 0=default  1=GPU  2=VIC
      queue: {}

visuals:
  enable: true
  elements:
    - id: tiler
      type: nvmultistreamtiler
      rows: 2
      columns: 2
      queue: {}
    - id: osd
      type: nvdsosd
      process_mode: 1
      queue: {}

outputs:
  - id: rtsp_out
    type: rtsp_client
    elements:
      - id: encoder
        type: nvv4l2h264enc
        bitrate: 3000000
      - id: sink
        type: rtspclientsink
        location: rtsp://192.168.1.99:8554/de1
        queue: {}

event_handlers:
  - id: smart_record
    enable: true
    type: on_detect
    probe_element: tracker
    trigger: smart_record
    label_filter: [car, person, truck]
  - id: crop_objects
    enable: true
    type: on_detect
    probe_element: tracker
    trigger: crop_object
    save_dir: "/opt/vms_engine/dev/rec/objects"
    capture_interval_sec: 5
```

> [!WARNING]
> All enum fields (`leaky`, `smart_record`, `process_mode`, `compute_hw`, etc.) must be **integers**, not strings. YAML uses `snake_case`; the parser maps to GStreamer's `kebab-case` internally.

---

## Development Workflow

### Typical iteration

```bash
docker compose exec app bash          # attach
# edit source files on host (auto-synced via volume mount)
cmake --build build -- -j5            # rebuild
./build/bin/vms_engine -c dev/configs/my_pipeline.yml
tail -f dev/logs/app.log
```

### Code formatting

```bash
./scripts/format.sh           # format all .cpp/.hpp with clang-format
./scripts/format.sh --check   # dry-run (non-zero exit if changes needed)
./scripts/install-hooks.sh    # install pre-commit hook (run once after clone)
```

### Production image

```bash
# Build Release binary inside the container
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream
cmake --build build -- -j5

# Build the production image from the host
exit
docker build -f Dockerfile.image -t vms-engine:latest .
```

> `Dockerfile.image` uses `nvcr.io/nvidia/deepstream:8.0-triton-multiarch` (no build tools). Only `build/bin/` is copied — the binary links spdlog and hiredis statically and is self-contained.

---

## Debugging & Troubleshooting

### GDB

```bash
gdb --args ./build/bin/vms_engine -c dev/configs/my_pipeline.yml
```

Enable core dumps in `docker-compose.yml`:
```yaml
ulimits:
  core: -1
```

### GStreamer diagnostics

```bash
# Verbose GStreamer log
GST_DEBUG=3 ./build/bin/vms_engine -c dev/configs/my_pipeline.yml

# Increase verbosity via config
pipeline:
  gst_log_level: "*:3,nvinfer:4,GST_ELEMENT_PADS:5"

# Visualize pipeline graph
dot -Tpng dev/logs/pipeline_PLAYING.dot -o /tmp/pipeline.png
```

### Common issues

| Symptom | Fix |
|---|---|
| `nvstreammux`, `nvinfer` element not found | Run `/opt/nvidia/deepstream/deepstream/user_additional_install.sh` |
| Engine load failed / model not found | Check `config_file` path; ensure batch size matches TensorRT engine |
| RTSP connect timeout | Test with `ffplay rtsp://...`; set `select_rtp_protocol: 4` (force TCP) |
| Out of GPU memory | Reduce `max_batch_size`; use a smaller model |
| `leaky` parse error | Must be integer (`2`), not string (`"downstream"`) |

---

## Quick Reference

```bash
# Container
docker build -t vms-engine-dev:latest .
docker compose up -d
docker compose exec app bash
docker compose down

# Build (inside container)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream
cmake --build build -- -j5

# Run
./build/bin/vms_engine -c dev/configs/my_pipeline.yml

# Debug
gdb --args ./build/bin/vms_engine -c dev/configs/my_pipeline.yml
GST_DEBUG=3 ./build/bin/vms_engine -c dev/configs/my_pipeline.yml
tail -f dev/logs/app.log
dot -Tpng dev/logs/pipeline_PLAYING.dot -o /tmp/pipeline.png

# Dev helpers
./scripts/format.sh
./scripts/install-hooks.sh

# Production image (from host, after Release build)
docker build -f Dockerfile.image -t vms-engine:latest .
```

---

## Related Services

| Service | Location | Role |
|---|---|---|
| **VMS FastAPI** | `vms_app_fastapi/` | Event processing, REST API, database |
| **Lantana Master** | `lantana_prj/services/lantana_master/` | Control plane — pipeline lifecycle management |
| **Lantana Worker** | `lantana_prj/services/lantana_worker/` | Pipeline config generation, model deployment |
| **GPU VMS Frontend** | `gpu-vms/` | Electron-based management UI |

---

**Architecture reference:** [`docs/architecture/ARCHITECTURE_BLUEPRINT.md`](docs/architecture/ARCHITECTURE_BLUEPRINT.md)  
**DeepStream deep-dives:** [`docs/architecture/deepstream/`](docs/architecture/deepstream/)  
**RAII and memory management:** [`docs/architecture/RAII.md`](docs/architecture/RAII.md)  
**Config schema:** [`docs/configs/deepstream_default.yml`](docs/configs/deepstream_default.yml)
