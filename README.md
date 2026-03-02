# VMS Engine

> High-performance Video Management System engine — **NVIDIA DeepStream 8.0** · **C++17** · **Config-Driven**

VMS Engine xử lý video realtime từ nhiều camera (RTSP, file, URI) với AI inference (object detection, tracking, analytics) và output ra display, file recording, RTSP streaming, và message broker.

---

## 📖 Table of Contents

- [VMS Engine](#vms-engine)
  - [📖 Table of Contents](#-table-of-contents)
  - [Key Features](#key-features)
  - [Architecture](#architecture)
  - [Prerequisites](#prerequisites)
  - [Container Setup](#container-setup)
    - [1. Tạo `.env` file](#1-tạo-env-file)
    - [2. Build dev image](#2-build-dev-image)
    - [3. Start container](#3-start-container)
    - [4. Attach vào container](#4-attach-vào-container)
  - [Build](#build)
    - [Configure CMake (lần đầu)](#configure-cmake-lần-đầu)
    - [Biên dịch](#biên-dịch)
    - [Clean build](#clean-build)
  - [Run](#run)
  - [Development Workflow](#development-workflow)
    - [Vòng lặp điển hình](#vòng-lặp-điển-hình)
    - [Auto-rebuild khi thay đổi file](#auto-rebuild-khi-thay-đổi-file)
    - [Tham chiếu config mẫu](#tham-chiếu-config-mẫu)
    - [Build production image](#build-production-image)
  - [Config Reference](#config-reference)
  - [Debugging \& Troubleshooting](#debugging--troubleshooting)
    - [GDB](#gdb)
    - [GStreamer logs](#gstreamer-logs)
    - [Common Issues](#common-issues)
  - [Quick Commands](#quick-commands)
  - [Related Services](#related-services)

---

## Key Features

- **DeepStream-Native** — GPU-accelerated video analytics với NVIDIA DeepStream SDK 8.0
- **Config-Driven** — Pipeline topology mô tả hoàn toàn qua YAML; zero code changes cho deployment mới
- **Multi-Stream** — Xử lý nhiều RTSP/URI sources đồng thời với `nvmultiurisrcbin`
- **AI Inference** — PGIE/SGIE qua TensorRT (`nvinfer`) hoặc Triton Inference Server (`nvinferserver`)
- **Smart Recording** — Recording theo event, có pre-event buffer
- **Clean Architecture** — Interface-first, dependency rule, không tight coupling với DeepStream nội bộ
- **Plugin System** — Runtime-loadable `.so` handlers cho custom event processing
- **Observable** — Structured logging (spdlog), DOT graph export, GStreamer bus monitoring

---

## Architecture

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
│           PipelineBuilder               │  ← Orchestrates sub-builders
└──────┬──────────┬──────────┬────────────┘
       ▼          ▼          ▼
┌────────────┐ ┌────────────┐ ┌────────────┐ ┌────────────┐
│  Source    │►│ Processing │►│  Visuals   │►│  Outputs   │
│  Builder   │ │  Builder   │ │  Builder   │ │  Builder   │
│ (nvmulti.. │ │ (nvinfer,  │ │ (tiler,   │ │ (rtsp,     │
│  nvmux)    │ │  tracker)  │ │  OSD)     │ │  file)     │
└────────────┘ └────────────┘ └────────────┘ └────────────┘
```

**Layer structure:** `app/` → `pipeline/` → `core/` ← `infrastructure/`, `domain/`, `services/`  
**Architecture doc:** [`docs/architecture/ARCHITECTURE_BLUEPRINT.md`](docs/architecture/ARCHITECTURE_BLUEPRINT.md)

---

## Prerequisites

**Host requirements:**
- NVIDIA GPU với driver **≥ 535**
- Docker + **NVIDIA Container Toolkit**
- VS Code + [Dev Containers extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers) (recommended)

**Docker images được build sẵn:**

| File             | Image tag              | Dùng khi nào                                |
| ---------------- | ---------------------- | ------------------------------------------- |
| `Dockerfile`     | `vms-engine-dev:latest`| **Development** — có build tools, gdb, valgrind |
| `Dockerfile.image` | `vms-engine:latest`  | **Production** — chỉ binary + DeepStream base, chạy qua orchestration service |

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

`docker-compose.yml` mount toàn bộ source code vào `/opt/lantana` — chỉnh code trên host là thấy ngay trong container, không cần rebuild image.

### 4. Attach vào container

```bash
docker exec -it vms-engine-dev bash
```

---

## Build

Mọi lệnh build đều chạy **bên trong container** tại working dir `/opt/lantana`.

### Configure CMake (lần đầu)

```bash
cmake -S . -B build \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream
```

**Tham số:**

| Parameter                    | Mô tả                                                      |
| ---------------------------- | ---------------------------------------------------------- |
| `-S .`                       | Source dir (chứa `CMakeLists.txt` root)                    |
| `-B build`                   | Build output dir                                           |
| `-G Ninja`                   | Dùng Ninja (nhanh hơn Make)                                |
| `-DCMAKE_BUILD_TYPE=Debug`   | `Debug` / `RelWithDebInfo` / `Release`                     |
| `-DDEEPSTREAM_DIR=...`       | Path đến DeepStream SDK (default trong container đã đúng)  |

### Biên dịch

```bash
# Giới hạn parallel jobs để tránh OOM (mỗi cc1plus tốn ~300–500 MB RAM)
cmake --build build -j4

# Hoặc dùng toàn bộ cores nếu RAM đủ
cmake --build build -j$(nproc)
```

Binary output: `build/bin/vms_engine`

### Clean build

```bash
rm -rf build/
# Rồi configure lại từ đầu
```

---

## Run

```bash
# Trong container, tại /opt/lantana
./build/bin/vms_engine -c dev/config/your_pipeline.yml
```

**Cấu trúc thư mục runtime:**

```
dev/                            # Git-ignored (chỉ .gitkeep được track)
├── config/                     # Pipeline YAML configs
│   └── your_pipeline.yml
├── models/                     # Models (TRT engines, ONNX, labels, ...)
│   └── my-model/
│       ├── config_infer_engine.yml
│       └── 1/
│           └── model.plan
├── rec/                        # Recording outputs (MP4)
└── logs/                       # App logs + GStreamer .dot files
```

> `dev/` là runtime data dir — toàn bộ nội dung bị gitignore. Chỉ `.gitkeep` được commit để giữ cấu trúc folder.

---

## Development Workflow

### Vòng lặp điển hình

```bash
# 1. Attach vào container
docker exec -it vms-engine-dev bash

# 2. Chỉnh code (editor trên host)

# 3. Rebuild (trong container)
cmake --build build -j4

# 4. Test
./build/bin/vms_engine -c dev/config/my_pipeline.yml

# 5. Xem logs nếu có lỗi
tail -f dev/logs/app.log
ls -lah dev/logs/*.dot          # GStreamer pipeline graphs
```

### Auto-rebuild khi thay đổi file

```bash
apt-get install -y entr

# Watch .cpp/.hpp và rebuild tự động
find . -name "*.cpp" -o -name "*.hpp" | grep -v build | entr -c cmake --build build -j4
```

### Tham chiếu config mẫu

Config mẫu được đặt trong `docs/configs/`:

```bash
# Copy mẫu để thử nghiệm
cp docs/configs/deepstream_default.yml dev/config/my_test.yml

# Chỉnh config
vim dev/config/my_test.yml

# Chạy
./build/bin/vms_engine -c dev/config/my_test.yml
```

### Build production image

```bash
# Build binary trước (Release mode)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDEEPSTREAM_DIR=/opt/nvidia/deepstream/deepstream
cmake --build build -j4

# Thoát container, build image production từ host
exit
docker build -f Dockerfile.image -t vms-engine:latest .
```

> `Dockerfile.image` dùng `nvcr.io/nvidia/deepstream:8.0-triton-multiarch` (lighter, no build tools). Copy `build/bin/` vào image và set `ENTRYPOINT ["./vms_engine"]`. Image này được orchestrat bởi service riêng — không dùng `docker-compose.yml` này.

---

## Config Reference

Pipeline config là một YAML file với các section chính:

```yaml
version: "1.0.0"

application:
  name: "my_pipeline"
  log_file: "/opt/lantana/dev/logs/app.log"
  gst_log_level: "*:1"               # 1=error ... 5=trace
  gst_dot_file_dir: "/opt/lantana/dev/logs"

sources:
  id: "main_source"
  type: "nvmultiurisrcbin"
  mode: 0                            # 0=rtsp, 1=file, 2=image
  uris:
    - "rtsp://192.168.1.10:554/stream"
    - "rtsp://192.168.1.11:554/stream"
  # nvmultiurisrcbin direct properties
  max_batch_size: 4
  # nvstreammux passthrough
  width: 1920
  height: 1080
  batched_push_timeout: 33333        # µs
  live_source: 1
  # nvurisrcbin passthrough
  latency: 100                       # ms
  rtsp_reconnect_interval: 5        # seconds

processing:
  elements:
    - id: "pgie"
      type: "nvinfer"                # nvinfer (TensorRT) | nvinferserver (Triton)
      process_mode: 1                # 1=Primary, 2=Secondary
      config_file_path: "/opt/lantana/dev/models/my-model/config_infer_engine.yml"
      batch_size: 4
      queue: {}

    - id: "tracker"
      type: "nvtracker"
      ll_lib_file: "/opt/nvidia/deepstream/deepstream/lib/libnvds_nvmultiobjecttracker.so"
      ll_config_file: "/opt/nvidia/deepstream/deepstream/samples/configs/deepstream-app/config_tracker_NvDCF_perf.yml"
      tracker_width: 640
      tracker_height: 384
      compute_hw: 1                  # 0=default, 1=GPU, 2=VIC
      queue: {}

visuals:
  tiler:
    enable: true
    rows: 2
    columns: 2
    width: 1920
    height: 1080
  osd:
    enable: true
    process_mode: 1                  # 0=cpu, 1=gpu, 2=auto

outputs:
  - id: "rtsp_output"
    type: "rtsp"
    elements:
      - type: "nvvideoconvert"
      - type: "capsfilter"
        caps: "video/x-raw(memory:NVMM),format=I420"
      - type: "nvv4l2h264enc"
        bitrate: 4000000
      - type: "h264parse"
      - type: "rtspclientsink"
        location: "rtsp://localhost:8554/stream"
```

**Reference đầy đủ:** [`docs/configs/deepstream_default.yml`](docs/configs/deepstream_default.yml)

---

## Debugging & Troubleshooting

### GDB

```bash
# Debug với GDB (build phải là Debug hoặc RelWithDebInfo)
gdb --args ./build/bin/vms_engine -c dev/config/my_pipeline.yml

# Debug core dump
gdb ./build/bin/vms_engine core
```

Enable core dumps trong `docker-compose.yml`:

```yaml
ulimits:
  core: -1
```

### GStreamer logs

Tăng log level trong config:

```yaml
application:
  gst_log_level: "*:3,GST_ELEMENT_PADS:5"  # Format: category:level
  gst_dot_file_dir: "/opt/lantana/dev/logs"
```

Convert `.dot` pipeline graph sang ảnh:

```bash
dot -Tpng dev/logs/pipeline_PLAYING.dot -o dev/logs/pipeline.png
```

### Common Issues

**Model not found:**
```
ERROR: Failed to load model at dev/models/.../model.plan
```
→ Kiểm tra path trong YAML config, đảm bảo file `.plan` tồn tại

**Batch size mismatch:**
```
ERROR: Batch size in config (4) doesn't match TensorRT engine (1)
```
→ Rebuild TensorRT engine với batch size đúng hoặc update `batch_size` trong config

**RTSP connect timeout:**
```
ERROR: Could not connect to rtsp://192.168.1.x:554/...
```
→ Kiểm tra network, test bằng `ffplay rtsp://...` hoặc VLC, check `rtsp_reconnect_interval`

**Out of GPU memory:**
```
ERROR: CUDA out of memory
```
→ Giảm `max_batch_size`, giảm số cameras, hoặc dùng model nhỏ hơn

**GStreamer element not found:**
```
WARNING: Could not load plugin 'nvstreammux'
```
→ Chạy `/opt/nvidia/deepstream/deepstream/user_additional_install.sh` trong container

---

## Quick Commands

```bash
# === Image & Container ===
docker build -t vms-engine-dev:latest .                   # Build dev image
docker compose up -d                                       # Start container
docker exec -it vms-engine-dev bash                       # Attach
docker compose down                                        # Stop container

# === Build ===
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug     # Configure (first time)
cmake --build build -j4                                    # Build
cmake --build build -j4 --target vms_engine               # Build specific target
rm -rf build/                                              # Clean

# === Run ===
./build/bin/vms_engine -c dev/config/my_pipeline.yml
./build/bin/vms_engine --help

# === Debug ===
gdb --args ./build/bin/vms_engine -c dev/config/my_pipeline.yml
tail -f dev/logs/app.log
dot -Tpng dev/logs/pipeline_PLAYING.dot -o /tmp/pipeline.png

# === Dev Helpers ===
cp docs/configs/deepstream_default.yml dev/config/test.yml      # Copy sample config
find . -name "*.cpp" -o -name "*.hpp" | grep -v build | entr -c cmake --build build -j4  # Auto-rebuild

# === Production Image ===
docker build -f Dockerfile.image -t vms-engine:latest .   # Build prod image

# === Cleanup ===
rm -f dev/logs/*.dot dev/logs/*.log                        # Clean logs
rm -rf dev/rec/*                                           # Clean recordings
```

---

## Related Services

| Service                    | Path                            | Role                                              |
| -------------------------- | ------------------------------- | ------------------------------------------------- |
| **VMS FastAPI**            | `vms_app_fastapi/`              | Event processing, REST API, database              |
| **Lantana Master**         | `lantana_prj/services/lantana_master/` | Control plane — quản lý pipelines       |
| **Lantana Worker**         | `lantana_prj/services/lantana_worker/` | Generates pipeline configs, deploys models |
| **GPU VMS Frontend**       | `gpu-vms/`                      | Electron app — UI management                      |

---

**Architecture:** [`docs/architecture/ARCHITECTURE_BLUEPRINT.md`](docs/architecture/ARCHITECTURE_BLUEPRINT.md)  
**Implementation Plans:** [`docs/plans/phase1_refactor/`](docs/plans/phase1_refactor/)  
**Config Schema:** [`docs/configs/deepstream_default.yml`](docs/configs/deepstream_default.yml)
