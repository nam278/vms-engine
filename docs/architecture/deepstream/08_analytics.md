# 08. Analytics — nvdsanalytics

## 1. Tổng quan

`nvdsanalytics` là GStreamer element xử lý **stream analytics** không cần AI inference — dùng geometric rules để detect events:

| Analytics Type | Mô tả | Use Case |
|---------------|-------|---------|
| **ROI (Region of Interest)** | Phát hiện object trong vùng polygon | Parking zone occupancy |
| **Line Crossing** | Đếm objects qua đường thẳng | Vehicle entry/exit counting |
| **Direction Detection** | Detect hướng di chuyển (vào/ra) | Gate direction |
| **Overcrowding** | Cảnh báo khi số người vượt threshold | Crowd safety |

## 2. Vị trí trong Pipeline

```
nvtracker.src ──► [queue] ──► nvdsanalytics ──► nvstreamdemux
                                    │
                              Thêm analytics metadata
                              vào NvDsObjectMeta
                              (vẫn cho buffer đi qua)
```

## 3. Config File Format

`nvdsanalytics` sử dụng config file riêng (`.txt`) theo GKeyFile format:

```ini
# configs/analytics/nvdsanalytics_config.txt

[property]
enable=1
config-width=1920           # Phải match pipeline width
config-height=1080
display-font-size=12
osd-mode=2                 # 0=CPU, 1=GPU, 2=HW

# ── ROI (Region of Interest) ─────────────────────────────
# Format: roi-<class>-<stream>=x1;y1;x2;y2;x3;y3;...
# Polygon coordinates (normalized 0.0–1.0 hoặc pixel)

[roi-filtering-stream-0]
enable=1
# Các polygon ROI cho stream 0
roi-0=0;0;400;0;400;400;0;400     # id=0, polygon top-left
roi-1=500;300;900;300;900;600;500;600  # id=1, polygon center

# Chỉ count objects thuộc class nào:
class-id=0;2    # 0=person, 2=vehicle; -1 = all classes

# ── Line Crossing ─────────────────────────────────────────
[line-crossing-stream-0]
enable=1
# Format: line-crossing-<id>=<x1>;<y1>;<x2>;<y2>;<direction>
# direction: EW (East-West) | NS (North-South) | ANY
line-crossing-0=960;0;960;1080;ANY    # Vertical center line

class-id=-1    # -1 = all classes

# Counting mode:
mode=balanced  # balanced | strict | loose

# ── Overcrowding ─────────────────────────────────────────
[overcrowding-stream-0]
enable=1
roi-0=0;0;1920;1080    # Toàn frame
class-id=0             # Chỉ người
object-threshold=10    # Cảnh báo khi > 10 người

# ── Direction Detection ───────────────────────────────────
[direction-detection-stream-0]
enable=1
class-id=2             # Vehicles
```

## 4. Analytics trong YAML Config

```yaml
# config.yml — phần processing
processing:
  elements:
    - id: "pgie"
      ...

    - id: "tracker"
      ...

    - id: "analytics"
      role: "analytics"
      queue: {}
      enabled: true
      config_file: "configs/analytics/nvdsanalytics_config.txt"
      gpu_id: 0

    - id: "demuxer"
      role: "demuxer"
      ...
```

## 5. AnalyticsBuilder Implementation

```cpp
// pipeline/include/engine/pipeline/builders/analytics_builder.hpp
class AnalyticsBuilder : public IElementBuilder {
public:
    GstElement* build(const PipelineConfig& config,
                      const std::string& id,
                      GstElement* pipeline) override {
        // Tìm analytics config
        const auto* analytics_cfg = find_by_id(config.processing, id);
        if (!analytics_cfg || !analytics_cfg->enabled) {
            LOG_D("Analytics '{}' is disabled, skipping", id);
            return nullptr;
        }

        auto elem = make_gst_element("nvdsanalytics", id.c_str());
        if (!elem) return nullptr;

        g_object_set(G_OBJECT(elem.get()),
            "config-file",    analytics_cfg->config_file.c_str(),
            "gpu-id",         analytics_cfg->gpu_id,
            nullptr);

        if (!gst_bin_add(GST_BIN(pipeline), elem.get())) return nullptr;
        LOG_I("nvdsanalytics '{}' built: config={}", id, analytics_cfg->config_file);
        return elem.release();
    }
};
```

## 6. NvDs Analytics Metadata

Sau khi qua `nvdsanalytics`, metadata được attach vào `NvDsObjectMeta`:

```cpp
// Trong pad probe sau nvdsanalytics (hoặc trong event handler):
GstPadProbeReturn read_analytics_probe(GstPad*, GstPadProbeInfo* info, gpointer) {
    auto* buf = GST_PAD_PROBE_INFO_BUFFER(info);
    auto* batch_meta = gst_buffer_get_nvds_batch_meta(buf);

    for (auto* fl = batch_meta->frame_meta_list; fl; fl = fl->next) {
        auto* frame = static_cast<NvDsFrameMeta*>(fl->data);

        for (auto* ol = frame->obj_meta_list; ol; ol = ol->next) {
            auto* obj = static_cast<NvDsObjectMeta*>(ol->data);

            // Analytics meta được lưu trong obj->misc_obj_info[]
            // Hoặc qua NvDsAnalyticsObjInfo (từ nvdsanalytics API)
            auto* analytics_meta = get_analytics_meta(obj);
            if (!analytics_meta) continue;

            // ROI
            if (analytics_meta->roiStatus) {
                for (const auto& [roi_id, is_in_roi] : analytics_meta->roiStatus) {
                    if (is_in_roi) {
                        LOG_D("Object {} is in ROI {}", obj->object_id, roi_id);
                    }
                }
            }

            // Line crossing
            if (analytics_meta->lcStatus) {
                for (const auto& [lc_id, direction] : analytics_meta->lcStatus) {
                    LOG_I("Object {} crossed line {} going {}",
                          obj->object_id, lc_id, direction);
                    // Publish event to Redis/Kafka
                    publish_crossing_event(frame, obj, lc_id, direction);
                }
            }

            // Direction
            if (!analytics_meta->dirStatus.empty()) {
                LOG_D("Object {} direction: {}", obj->object_id,
                      analytics_meta->dirStatus);
            }
        }

        // Overcrowding (frame-level)
        auto* frame_analytics = get_frame_analytics_meta(frame);
        if (frame_analytics && frame_analytics->ocStatus) {
            LOG_W("Overcrowding detected in stream {}! Objects={}",
                  frame->source_id, frame_analytics->object_count);
            send_overcrowding_alert(frame->source_id);
        }
    }

    return GST_PAD_PROBE_OK;
}
```

## 7. Runtime Config Reload

`nvdsanalytics` hỗ trợ reload config khi đang chạy (không cần restart pipeline):

```cpp
// Sau khi thay đổi analytics_config.txt:
void PipelineManager::reload_analytics_config(const std::string& new_config_path) {
    GstElement* analytics = gst_bin_get_by_name(
        GST_BIN(pipeline_), "analytics");

    if (!analytics) {
        LOG_W("Analytics element not found");
        return;
    }

    // Set property mới → nvdsanalytics tự reload
    g_object_set(analytics, "config-file", new_config_path.c_str(), nullptr);
    LOG_I("Analytics config reloaded: {}", new_config_path);

    gst_object_unref(analytics);
}
```

## 8. Tích hợp với Message Broker

Analytics events thường được publish tới Redis/Kafka qua `nvmsgconv` + `nvmsgbroker`. Analytics metadata trong `NvDsObjectMeta` được serialize tự động bởi `nvmsgconv`:

```yaml
# config.yml
message_broker:
  enabled: true
  msgconv:
    payload_type: 1          # MINIMAL schema
    config: "configs/msgconv_config.txt"
  broker:
    proto_lib: ".../libnvds_redis_proto.so"
    conn_str: "localhost;6379;vms_events"
    topic: "vms/analytics"
```

### Minimal Payload JSON Output

```json
{
  "version": "4.0",
  "type": "object",
  "id": "550e8400-e29b-41d4-a716-446655440000",
  "timestamp": "2024-01-15T10:30:00.000Z",
  "sensorId": "cam_01",
  "object": {
    "id": "1234",
    "confidence": 0.92,
    "bbox": {"topleftx": 100, "toplefty": 200, "width": 80, "height": 120},
    "type": "Person"
  },
  "analyticsModule": {
    "roiStatus": {"zone_entrance": true},
    "lineCrossingStatus": {},
    "overcrowdingStatus": false
  }
}
```

## 9. Performance Considerations

- `nvdsanalytics` chạy trên CPU (process_mode=0) hoặc GPU — CPU mode đủ cho đa số use cases
- Số ROI và lines crossing ảnh hưởng linear đến CPU usage
- Không cần GPU inference → độ trễ rất thấp (~0.1–0.5ms per batch)
- Nên đặt **sau tracker** để có tracking IDs (required cho line crossing counting)

## 10. Ví dụ: Parking Lot Analytics

```ini
# Cấu hình ROI cho bãi đỗ xe với 4 khu vực + đường vào

[roi-filtering-stream-0]
enable=1
# Zone A: 20 chỗ góc trái
roi-zone-a=0;400;480;400;480;720;0;720
# Zone B: 20 chỗ góc phải
roi-zone-b=840;400;1280;400;1280;720;840;720
# Entrance lane
roi-entrance=560;0;720;0;720;300;560;300
class-id=2    # Vehicles only

[line-crossing-stream-0]
enable=1
# Entry gate (horizontal)
line-crossing-entry=560;250;720;250;ANY
# Exit gate (horizontal, opposite direction)
line-crossing-exit=560;50;720;50;ANY
class-id=2
mode=strict

[overcrowding-stream-0]
enable=1
roi-0=0;0;1280;720
class-id=2
object-threshold=38    # Cảnh báo khi bãi > 95% capacity (40 chỗ)
```
