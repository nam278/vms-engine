# 00. VMS Engine — Tổng quan dự án

## 1. Giới thiệu

**VMS Engine** là một ứng dụng Video Management System engine được xây dựng trên nền tảng **NVIDIA DeepStream SDK 8.0**, sử dụng **C++17** và **GStreamer 1.0**. Ứng dụng xử lý video thời gian thực từ nhiều nguồn (RTSP, file, URI) với AI inference (object detection, tracking, analytics) và xuất ra display, file recording, RTSP streaming, và message brokers.

Đây là **phiên bản refactor** của `lantanav2` với các cải tiến chính:

| Mục | lantanav2 (cũ) | vms-engine (mới) |
|-----|----------------|------------------|
| Namespace | `lantana::` | `engine::` |
| Include prefix | `lantana/core/` | `engine/core/` |
| DeepStream SDK | 7.1 | **8.0** |
| Backend | DeepStream + DLStreamer (multi-backend) | **DeepStream-native only** |
| Config variants | `std::variant` cho backend options | Config struct trực tiếp (không variant) |
| Pipeline layer | `backends/deepstream/` | `pipeline/` |
| Executable | `lantana` | `vms_engine` |

## 2. Tech Stack

| Component | Technology | Phiên bản |
|-----------|-----------|-----------|
| Language | C++17 | - |
| Build System | CMake + vcpkg | 3.16+ |
| Video Framework | GStreamer | 1.0 |
| AI Backend | NVIDIA DeepStream SDK | **8.0** |
| GPU Inference | TensorRT, CUDA | - |
| Ext. Inference | Triton Inference Server | - |
| Configuration | YAML (yaml-cpp) | - |
| Logging | spdlog + fmt | - |
| Messaging | Redis Streams, Kafka | - |
| Storage | Local FS, S3 (MinIO) | - |
| REST API | Pistache HTTP | - |

## 3. Tính năng chính

### 3.1 Video Input

- **Multi-source**: `nvmultiurisrcbin` — xử lý batching nội bộ (không cần `nvstreammux` tách biệt)
- **Dynamic add/remove**: thêm/bỏ camera qua REST API khi đang chạy
- **RTSP reconnect**: tự động kết nối lại với các thông số cấu hình

### 3.2 AI Processing

- **Primary Inference (PGIE)**: Object detection toàn frame — `nvinfer` (TensorRT) hoặc `nvinferserver` (Triton)
- **Secondary Inference (SGIE)**: Classification, LPR, attribute recognition — per-object
- **Tracker**: NvDCF, IOU, DeepSORT qua `nvtracker`
- **Analytics**: ROI filtering, line crossing, overcrowding, direction detection qua `nvdsanalytics`

### 3.3 Output & Recording

- **Display**: `nveglglessink` (X11), `nv3dsink`
- **File Recording**: MP4, MKV với H.264/H.265 encoding
- **RTSP Streaming**: `rtspclientsink`
- **Smart Record**: Pre/post event recording với buffer `nvmultiurisrcbin` tích hợp
- **Message Broker**: Redis Streams, Kafka qua `nvmsgconv` + `nvmsgbroker`

### 3.4 Runtime Control

- **REST API** (Pistache): thêm/bỏ camera, trigger smart record, query status
- **Runtime Parameters**: thay đổi inference interval, threshold, tracking params

## 4. Kiến trúc tổng quan

```
┌─────────────────────────────────────────────────────────────────────────┐
│                              main.cpp                                    │
│                         (Application Entry)                              │
└──────────────────────────────┬──────────────────────────────────────────┘
                               │
                               ▼
┌──────────────────────────────────────────────────────────────────────────┐
│                         PipelineManager                                  │
│                    (IPipelineManager impl)                               │
│         - Lifecycle: init, start, stop, pause                           │
│         - GstBus message routing                                         │
│         - Event handler registration                                     │
└──────────────────────────────┬───────────────────────────────────────────┘
                               │
                               ▼
┌──────────────────────────────────────────────────────────────────────────┐
│                         PipelineBuilder                                  │
│                    (IPipelineBuilder impl)                               │
│              - Điều phối build theo 5 phases                             │
│              - Quản lý `tails_` map (upstream endpoints)                │
└──────────────────────────────┬───────────────────────────────────────────┘
                               │
          ┌────────────────────┼────────────────────┐
          ▼                    ▼                     ▼
┌──────────────────┐ ┌──────────────────┐ ┌──────────────────┐
│  SourceBuilder   │►│ProcessingBuilder │►│  VisualsBuilder  │
│  Phase 1         │ │  Phase 2         │ │  Phase 3         │
│  - nvmultiuris.  │ │  - nvinfer(pgie) │ │  - nvtiler       │
│    rcbin         │ │  - nvtracker     │ │  - nvdsosd       │
└──────────────────┘ │  - nvinfer(sgie) │ └────────┬─────────┘
                     │  - nvdsanalytics │          │
                     │  - nvstreamdemux │          ▼
                     └──────────────────┘ ┌──────────────────┐
                                          │  OutputsBuilder  │
                                          │  Phase 4         │
                                          │  - encoders      │
                                          │  - sinks         │
                                          └────────┬─────────┘
                                                   │
                                                   ▼
                                          ┌──────────────────┐
                                          │StandaloneBuilder │
                                          │  Phase 5         │
                                          │  - smart record  │
                                          │  - msgconv/broker│
                                          └──────────────────┘
```

## 5. Luồng xử lý Pipeline

```
nvmultiurisrcbin
  │  (decode + mux nhiều RTSP → batched frames)
  ▼
queue → nvinfer (pgie — primary detection)
  ▼
queue → nvtracker
  ▼
[queue → nvinfer (sgie — secondary, optional)]
  ▼
[queue → nvdsanalytics (ROI/line crossing, optional)]
  ▼
nvstreamdemux (tách batch → per-stream outputs)
  │
  ├─► tee ──┬──► nvmultistreamtiler → nvdsosd ──► encoder → rtspclientsink
  │         └──► [encoder → filesink]
  │
  └─► [nvdssmartrecordbin] → nvmsgconv → nvmsgbroker → Redis/Kafka
```

## 6. Dependency Rule (Clean Architecture)

```
┌────────────────────────────────────────────────────┐
│      app/   (main.cpp — Entry point)               │
│      Wires tất cả layers lại với nhau              │
└────────────────────┬───────────────────────────────┘
                     │ depends on
┌────────────────────▼───────────────────────────────┐
│      pipeline/   (Builder & Manager)               │
│      Thi công GStreamer pipeline từ config          │
└────────────────────┬───────────────────────────────┘
                     │ depends on
┌────────────────────▼───────────────────────────────┐
│      core/   (Interfaces / Ports)          ◄── CENTER │
│      IPipelineManager, IBuilderFactory              │
│      IElementBuilder, Config types                  │
│      Logger (LOG_* macros)                          │
└────────────────────────────────────────────────────┘
                     ▲
                     │ implements
┌────────────────────┴───────────────────────────────┐
│      infrastructure/   (Adapters)                  │
│      YAML Parser, Redis, Kafka, S3, REST API       │
└────────────────────────────────────────────────────┘
```

**Quy tắc tuyệt đối**: `core/` không bao giờ include header của `pipeline/`, `infrastructure/`, hay `services/`.

## 7. Namespace convention

```
engine::core::pipeline      → core/include/engine/core/pipeline/
engine::core::builders      → core/include/engine/core/builders/
engine::core::config        → core/include/engine/core/config/
engine::pipeline            → pipeline/include/engine/pipeline/
engine::domain              → domain/include/engine/domain/
engine::infrastructure      → infrastructure/include/engine/infrastructure/
engine::services            → services/include/engine/services/
```

Logging macros (dùng `LOG_*` với underscore — khác với lantanav2 dùng `LOG*`):

```cpp
#include "engine/core/utils/logger.hpp"

LOG_T("Trace detail");
LOG_D("Debug: element={}", name);
LOG_I("Info: pipeline started, sources={}", count);
LOG_W("Warning: deprecated config key '{}'", key);
LOG_E("Error: gst_element_factory_make failed: {}", element_name);
LOG_C("Critical: pipeline init failed — aborting");
```
