# VMS Engine

> High-performance Video Management System engine — **NVIDIA DeepStream 8.0** · **C++17** · **Config-Driven**

VMS Engine xử lý video realtime từ nhiều camera (RTSP, file, URI) với AI inference (object detection, tracking, analytics) và output ra RTSP streaming, file recording, và message broker (Redis/Kafka).

---

## 📖 Table of Contents

- [Key Features](#key-features)
- [Architecture](#architecture)
- [Prerequisites](#prerequisites)
- [Container Setup](#container-setup)
- [Build](#build)
- [Run](#run)
- [Development Workflow](#development-workflow)
- [Config Reference](#config-reference)
- [Debugging & Troubleshooting](#debugging--troubleshooting)
- [Quick Commands](#quick-commands)
- [Related Services](#related-services)

---

## Key Features

- **DeepStream-Native** — GPU-accelerated video analytics với NVIDIA DeepStream SDK 8.0
- **Config-Driven** — Pipeline topology mô tả hoàn toàn qua YAML; zero code changes cho deployment mới
- **Multi-Stream** — Xử lý nhiều RTSP/URI sources đồng thời với `nvmultiurisrcbin`
- **AI Inference** — PGIE/SGIE qua TensorRT (`nvinfer`) hoặc Triton Inference Server (`nvinferserver`)
- **Smart Recording** — Recording theo event, có pre-event buffer tích hợp trong `nvmultiurisrcbin`
- **Event Handlers & Probes** — Pad probe callbacks (object crop, smart record trigger) + plugin `.so` runtime-loadable
- **Clean Architecture** — Interface-first, dependency rule, không tight coupling với DeepStream nội bộ
- **Observable** — Structured logging (spdlog), DOT graph export, GStreamer bus monitoring

---

## Architecture

### Pipeline Topology

```
[sources_bin]        nvmultiurisrcbin (decode + mux → batched frames)
     │                 ghost src pad
     │
     ▼
[processing_bin]     queue→nvinfer(pgie)→queue→nvtracker
                     [queue→nvinfer(sgie)] (optional)
     │                 ghost sink+src pads
     │
     ▼
[visuals_bin]        queue→nvmultistreamtiler→queue→nvdsosd
                     (optional — skip if visuals.enable: false)
     │                 ghost sink+src pads
     │
     ▼
[output_bin_{id}]    queue→nvvideoconvert→capsfilter→
                     nvv4l2h264enc→h264parse→queue→rtspclientsink
                     ghost sink pad
```

Mỗi stage là một `GstBin` độc lập với ghost pads. `tails_["src"]` lưu bin pointer của stage hiện tại để link với stage tiếp theo.

### Builder System (5 Phases)

Mỗi phase tạo một **GstBin** với ghost pads, sau đó link bin đó với bin của phase trước via `gst_element_link()`.

```
┌─────────────────────────────────────────┐
│              main.cpp                   │
│          (App Entry Point)              │
└─────────────────┬───────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│           PipelineManager               │  ← Lifecycle, GstBus, event handlers
└─────────────────┬───────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│           PipelineBuilder               │  ← Điều phối 5 phases, quản lý tails_ map
└──────┬──────────┬──────────┬────────────┘
       ▼          ▼          ▼            ▼            ▼
┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────┐
│ Source   │►│Processing│►│ Visuals  │►│ Outputs  │►│ Standalone   │
│ Phase 1  │ │ Phase 2  │ │ Phase 3  │ │ Phase 4  │ │ Phase 5      │
│sources_  │ │processing│ │visuals_  │ │output_   │ │smart record  │
│bin (Bin) │ │_bin (Bin)│ │bin (Bin) │ │bin_{id}  │ │msgconv/broker│
└──────────┘ │nvinfer   │ └──────────┘ │  (Bin)   │ └──────────────┘
             │nvtracker │              └──────────┘
             └──────────┘
```

`tails_["src"]` luôn trỏ đến bin mới nhất. Bins link với nhau qua ghost pads.

### Layer Dependency

```
app/            → depends on: pipeline, infrastructure, core
pipeline/       → depends on: core ONLY
infrastructure/ → depends on: core ONLY
domain/         → depends on: core ONLY
services/       → depends on: core ONLY
core/           → NO external deps (only std + GStreamer fwd-declares)
```

**Quy tắc tuyệt đối**: `core/` không bao giờ include header của `pipeline/`, `infrastructure/`, hay `services/`.

**Architecture docs đầy đủ:** [`docs/architecture/`](docs/architecture/)

---

## Prerequisites

**Host requirements:**

- NVIDIA GPU với driver **≥ 535**
- Docker + **NVIDIA Container Toolkit**
- VS Code + [Dev Containers extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers) (recommended)

**Docker images được build sẵn:**

| File               | Image tag               | Dùng khi nào                                                                  |
| ------------------ | ----------------------- | ----------------------------------------------------------------------------- |
| `Dockerfile`       | `vms-engine-dev:latest` | **Development** — có build tools, gdb, valgrind                               |
| `Dockerfile.image` | `vms-engine:latest`     | **Production** — chỉ binary + DeepStream base, chạy qua orchestration service |

---

## Container Setup

### 1. Tạo `.env` file

```bash
echo "APP_UID=$(id -u)" > .env
echo "APP_GID=$(id -g)" >> .env
```

### 2. Build dev image

```bash
docker build -t vms-engine-dev:latest .
```

> `Dockerfile` dùng `nvcr.io/nvidia/deepstream:8.0-gc-triton-devel` — full dev toolkit với build tools, gdb, valgrind.

### 3. Start container

```bash
docker compose up -d

# Kiểm tra container đã chạy
docker ps --filter name=vms-engine-dev
```

`docker-compose.yml` mount toàn bộ source code vào `/opt/vms_engine` — chỉnh code trên host là thấy ngay trong container, không cần rebuild image.

### 4. Attach vào container

```bash
docker compose exec app bash
```

---

## Build

Mọi lệnh build đều chạy **bên trong container** tại `/opt/vms_engine`.

### Configure CMake (lần đầu)

```bash
cmake -S . -B build \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream
```

| Parameter                         | Mô tả                                             |
| --------------------------------- | ------------------------------------------------- |
| `-G Ninja`                        | Dùng Ninja (nhanh hơn Make)                       |
| `-DCMAKE_BUILD_TYPE=Debug`        | `Debug` / `RelWithDebInfo` / `Release`            |
| `-DCMAKE_EXPORT_COMPILE_COMMANDS` | Sinh `compile_commands.json` cho clangd / IDE     |
| `-DDEEPSTREAM_DIR=...`            | Path đến DeepStream SDK (đã đúng trong container) |

### Biên dịch

```bash
# Giới hạn jobs để tránh OOM (mỗi cc1plus ~300–500 MB RAM)
cmake --build build -- -j5

# Dùng toàn bộ cores nếu RAM đủ
cmake --build build -- -j$(nproc)
```

Binary output: `build/bin/vms_engine`

Build output structure:

```
build/bin/
├── vms_engine          ← executable (self-contained, no build/lib/ deps)
├── configs/            ← copy của configs/ (tự động sau build)
├── logs/               ← tạo sẵn khi build
└── plugins/            ← 7 DeepStream parser .so
    ├── libnvdsinfer_custom_impl_Yolo.so
    ├── libocr_fast_plate_parser.so
    └── ...
```

### Clean build

```bash
rm -rf build/
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream
cmake --build build -- -j5
```

---

## Run

```bash
# Trong container, tại /opt/vms_engine
./build/bin/vms_engine -c dev/configs/my_pipeline.yml
```

**Cấu trúc thư mục runtime `dev/`:**

```
dev/                            # Git-ignored (chỉ .gitkeep được track)
├── configs/                    # Pipeline YAML configs
├── models/                     # TensorRT engines, ONNX, labels
│   └── phat-hien-nguoi-xe/
│       ├── config.pbtxt
│       ├── labels.txt
│       └── 1/
│           ├── model.onnx
│           └── model.onnx_b9_gpu0_fp32.engine
├── pipeline_components/        # Per-component nvinfer configs
│   ├── pgie_phat-hien-nguoi-xe_DETECTION/
│   └── sgie_nhan-dien-nguoi_MULTILABEL_CLASSIFICATION/
├── rec/                        # Smart record MP4 outputs
│   └── objects/                # Object crop JPEGs
├── config/                     # Tracker + misc runtime configs
└── logs/                       # App logs + GStreamer .dot graphs
```

> `dev/` là runtime data dir — toàn bộ nội dung bị gitignore. Chỉ `.gitkeep` được commit để giữ cấu trúc folder.

---

## Development Workflow

### Vòng lặp điển hình

```bash
# 1. Attach vào container
docker compose exec app bash

# 2. Chỉnh code (editor trên host — mount tự động sync)

# 3. Rebuild
cmake --build build -- -j5

# 4. Test
./build/bin/vms_engine -c dev/configs/my_pipeline.yml

# 5. Xem logs
tail -f dev/logs/app.log
ls -lah dev/logs/*.dot          # GStreamer pipeline graphs
```

### Dùng config mẫu

```bash
# Copy config mẫu
cp docs/configs/deepstream_default.yml dev/configs/my_test.yml

# Chỉnh camera URIs, paths, ...
vim dev/configs/my_test.yml

# Chạy
./build/bin/vms_engine -c dev/configs/my_test.yml
```

### Code formatting

```bash
# Format toàn bộ .cpp/.hpp (dùng clang-format)
./scripts/format.sh

# Dry-run check (không sửa file)
./scripts/format.sh --check

# Cài pre-commit hook (chạy 1 lần sau clone)
./scripts/install-hooks.sh
```

### Build production image

```bash
# Build binary Release trước (trong container)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream
cmake --build build -- -j5

# Thoát container, build image production từ host
exit
docker build -f Dockerfile.image -t vms-engine:latest .
```

> `Dockerfile.image` dùng `nvcr.io/nvidia/deepstream:8.0-triton-multiarch` (lighter, no build tools). Copy chỉ `build/bin/` vào image — binary đã self-contained (spdlog + hiredis link static).

---

## Config Reference

Config là YAML file, pass qua `-c path/to/config.yml`. Reference đầy đủ: [`docs/configs/deepstream_default.yml`](docs/configs/deepstream_default.yml)

```yaml
version: "1.0.0"

pipeline:
  id: "de1"
  name: "Intrusion Detection Pipeline"
  log_level: "INFO" # DEBUG | INFO | WARN | ERROR
  gst_log_level: "*:1" # GStreamer log categories
  dot_file_dir: "/opt/vms_engine/dev/logs"
  log_file: "/opt/vms_engine/dev/logs/app.log"

queue_defaults:
  max_size_buffers: 10
  leaky: 2 # 0=none  1=upstream  2=downstream

sources:
  type: nvmultiurisrcbin
  # NOTE: ip_address and port are NOT configured — DS8 ip-address setter causes SIGSEGV.
  max_batch_size: 4
  mode: 0 # 0=video  1=audio
  width: 1920
  height: 1080
  cameras:
    - id: camera-01
      uri: rtsp://192.168.1.99:8554/stream
  smart_record: 2 # 0=off  1=cloud  2=multi(cloud+local)
  smart_rec_dir_path: "/opt/vms_engine/dev/rec"

processing:
  elements:
    - id: pgie_detection
      type: nvinfer # nvinfer (TensorRT) | nvinferserver (Triton)
      role: primary_inference
      config_file: "/opt/vms_engine/dev/pipeline_components/pgie_detection/config.yml"
      process_mode: 1 # 1=Primary (full-frame)
      batch_size: 4
      queue: {}

    - id: tracker
      type: nvtracker
      ll_lib_file: "/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so"
      ll_config_file: "/opt/vms_engine/dev/config/tracker_NvDCF_perf.yml"
      tracker_width: 640
      tracker_height: 640
      compute_hw: 1 # 0=default  1=GPU  2=VIC
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

**Lưu ý quan trọng:**

- `leaky` là **integer** (`0`/`1`/`2`), không dùng string `"downstream"`
- Tất cả enum fields (`smart_record`, `process_mode`, `compute_hw`, ...) đều là **integer**
- YAML dùng `snake_case`; GStreamer property dùng `kebab-case` — parser tự map

---

## Debugging & Troubleshooting

### GDB

```bash
# Debug với GDB (build phải là Debug hoặc RelWithDebInfo)
gdb --args ./build/bin/vms_engine -c dev/configs/my_pipeline.yml

# Debug core dump
gdb ./build/bin/vms_engine core
```

Enable core dumps trong `docker-compose.yml`:

```yaml
ulimits:
  core: -1
```

### GStreamer logs

Tăng `gst_log_level` trong config:

```yaml
pipeline:
  gst_log_level: "*:3,nvinfer:4,GST_ELEMENT_PADS:5"
```

Convert `.dot` pipeline graph sang ảnh:

```bash
dot -Tpng dev/logs/pipeline_PLAYING.dot -o /tmp/pipeline.png
```

### Common Issues

**GStreamer element not found** (`nvstreammux`, `nvinfer`, ...):

```
→ Chạy: /opt/nvidia/deepstream/deepstream/user_additional_install.sh
```

**Model not found / engine load failed:**

```
→ Kiểm tra config_file path trong YAML, đảm bảo .onnx hoặc .engine tồn tại
→ Batch size trong config phải khớp với TensorRT engine
```

**RTSP connect timeout:**

```
→ Test: ffplay rtsp://... hoặc VLC
→ Check select_rtp_protocol: 4  (force TCP)
→ Check rtsp_reconnect_interval, rtsp_reconnect_attempts
```

**Out of GPU memory:**

```
→ Giảm max_batch_size, giảm số cameras
→ Dùng model nhỏ hơn hoặc giảm num_extra_surfaces
```

**`leaky` parse fail / queue không hoạt động đúng:**

```
→ Đảm bảo leaky là integer (2), không phải string ("downstream")
```

---

## Quick Commands

```bash
# === Image & Container ===
docker build -t vms-engine-dev:latest .          # Build dev image
docker compose up -d                              # Start container
docker compose exec app bash                      # Attach
docker compose down                               # Stop

# === Build (trong container) ===
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream   # Configure
cmake --build build -- -j5                        # Build
rm -rf build/                                     # Clean

# === Run ===
./build/bin/vms_engine -c dev/configs/my_pipeline.yml

# === Debug ===
gdb --args ./build/bin/vms_engine -c dev/configs/my_pipeline.yml
GST_DEBUG=3 ./build/bin/vms_engine -c dev/configs/my_pipeline.yml
tail -f dev/logs/app.log
dot -Tpng dev/logs/pipeline_PLAYING.dot -o /tmp/pipeline.png

# === Dev Helpers ===
cp docs/configs/deepstream_default.yml dev/configs/test.yml
./scripts/format.sh                               # Format code
./scripts/install-hooks.sh                        # Cài pre-commit hook

# === Production Image (từ host) ===
docker build -f Dockerfile.image -t vms-engine:latest .

# === Cleanup ===
rm -f dev/logs/*.dot dev/logs/*.log
rm -rf dev/rec/*
```

---

## Related Services

| Service              | Path                                   | Role                                 |
| -------------------- | -------------------------------------- | ------------------------------------ |
| **VMS FastAPI**      | `vms_app_fastapi/`                     | Event processing, REST API, database |
| **Lantana Master**   | `lantana_prj/services/lantana_master/` | Control plane — quản lý pipelines    |
| **Lantana Worker**   | `lantana_prj/services/lantana_worker/` | Sinh pipeline configs, deploy models |
| **GPU VMS Frontend** | `gpu-vms/`                             | Electron app — UI management         |

---

**Architecture:** [`docs/architecture/ARCHITECTURE_BLUEPRINT.md`](docs/architecture/ARCHITECTURE_BLUEPRINT.md)  
**DeepStream Deep-dives:** [`docs/architecture/deepstream/`](docs/architecture/deepstream/)  
**Implementation Plans:** [`docs/plans/phase1_refactor/`](docs/plans/phase1_refactor/)  
**Config Schema:** [`docs/configs/deepstream_default.yml`](docs/configs/deepstream_default.yml)
