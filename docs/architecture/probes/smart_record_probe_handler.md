# SmartRecord Probe Handler

## 1. Tổng Quan

`SmartRecordProbeHandler` là pad probe kích hoạt **NvDsSR smart recording** khi phát hiện object khớp với `label_filter`. Handler gắn vào `src` pad của element được cấu hình trong `probe_element` (thường là `tracker`) và gọi GSignal `start-sr` trực tiếp trên `nvurisrcbin` con bên trong `nvmultiurisrcbin`.

### Vai trò trong pipeline

```
nvmultiurisrcbin → [decode → mux] → nvinfer(PGIE) → nvtracker ─── src pad ───▶ OSD → sink
                                                                        │
                                                            SmartRecordProbeHandler
                                                            (attached to tracker:src)
                                                                        │
                                                              nvurisrcbin[source_id]
                                                              │
                                                              ├─ start-sr()  → recording starts
                                                              └─ sr-done sig ← recording done
```

Handler định kỳ kiểm tra mọi buffer đi qua — khi tìm thấy object khớp, nó tìm `nvurisrcbin` tương ứng với `source_id` đó và phát tín hiệu bắt đầu ghi hình.

---

## 2. Cấu Hình YAML

### 2.1 sources — Bật smart-record trên nvmultiurisrcbin

```yaml
sources:
  type: nvmultiurisrcbin
  smart_record: 2 # 0=disabled, 1=cloud-only, 2=multi (PHẢI > 0)
  smart_rec_dir_path: "/opt/engine/data/rec"
  smart_rec_file_prefix: "vms_rec"
  smart_rec_cache: 10 # pre-event circular buffer (giây)
  smart_rec_default_duration: 30
```

> ⚠️ `smart_record` trên `nvmultiurisrcbin` PHẢI được bật — nếu `smart_record = 0`, GSignal `start-sr` sẽ không có hiệu lực.

### 2.2 event_handlers — SmartRecord handler entry

```yaml
event_handlers:
  - id: smart_record
    enable: true
    type: on_detect
    probe_element: tracker # Attach probe tới element có id này
    source_element: nvmultiurisrcbin0 # Tên nvmultiurisrcbin trong pipeline
    trigger: smart_record # pad_name mặc định là "src"
    channel: worker_lsr # Redis Stream / Kafka topic; để trống = không publish
    label_filter:
      - car
      - person
      - truck
    pre_event_sec: 5 # Giây trước event được lưu (từ circular buffer)
    post_event_sec: 20 # Giây sau event
    min_interval_sec: 60 # Khoảng cách tối thiểu giữa hai lần ghi cùng một source
    max_concurrent_recordings: 4 # 0 = không giới hạn
```

### 2.3 Field Reference

| Field                       | Type     | Default | Mô tả                                                              |
| --------------------------- | -------- | ------- | ------------------------------------------------------------------ |
| `id`                        | string   | —       | Unique ID trong `event_handlers`                                   |
| `enable`                    | bool     | —       | `false` = bỏ qua handler này hoàn toàn                             |
| `probe_element`             | string   | —       | ID của element để gắn probe (thường là `tracker`)                  |
| `source_element`            | string   | —       | Tên `nvmultiurisrcbin` trong pipeline (để tìm nvurisrcbin)         |
| `trigger`                   | string   | —       | Phải là `"smart_record"`                                           |
| `pad_name`                  | string   | `"src"` | Pad để gắn probe (`"src"` hoặc `"sink"`)                           |
| `label_filter`              | string[] | `[]`    | Danh sách label khớp; rỗng = khớp tất cả                           |
| `pre_event_sec`             | int      | `2`     | Giây trước event (từ circular buffer của nvurisrcbin)              |
| `post_event_sec`            | int      | `20`    | Giây sau event                                                     |
| `min_interval_sec`          | int      | `2`     | Khoảng cách tối thiểu giữa 2 lần ghi của cùng 1 source             |
| `max_concurrent_recordings` | int      | `0`     | Số lượng recording song song tối đa; `0` = không giới hạn          |
| `channel`                   | string   | `""`    | Redis Stream / Kafka topic để publish events; rỗng = không publish |

### 2.4 Event Publishing & Broker Reconnect

Khi `channel` được cấu hình, handler publish **hai loại event** qua `IMessageProducer::publish(channel, json)`. Field names tương thích với format của lantanav2.

**`record_started`** — phát ngay khi ghi hình bắt đầu:

```json
{
  "event": "record_started",
  "pid": "pipeline-01",
  "sid": 0,
  "sname": "camera-01",
  "session_id": 42,
  "start_time": 5,
  "duration": 25000,
  "trigger_obj": 12345,
  "event_ts": 1741000000000
}
```

| Field         | Kiểu   | Mô tả                                                                    |
| ------------- | ------ | ------------------------------------------------------------------------ |
| `event`       | string | Luôn `"record_started"`                                                  |
| `pid`         | string | Pipeline ID (`config.pipeline.id`)                                       |
| `sid`         | int    | Source index (0-based)                                                   |
| `sname`       | string | Tên camera từ `sources.cameras[sid].id`                                  |
| `session_id`  | int    | Session ID trả về từ `start-sr` GSignal                                  |
| `start_time`  | int    | `pre_event_sec` — số giây back-fill từ circular buffer                   |
| `duration`    | int    | `(pre_event_sec + post_event_sec) * 1000` — tổng duration (milliseconds) |
| `trigger_obj` | int    | Tracker `object_id` đã kích hoạt recording                               |
| `event_ts`    | int    | Unix epoch milliseconds (`std::chrono::system_clock`)                    |

**`record_done`** — phát khi `sr-done` signal từ nvurisrcbin:

```json
{
  "event": "record_done",
  "pid": "pipeline-01",
  "sid": 0,
  "sname": "camera-01",
  "session_id": 42,
  "width": 1920,
  "height": 1080,
  "filename": "/opt/engine/data/rec/vms_rec_cam0_1741000000.mp4",
  "duration": 25000000000,
  "event_ts": 1741000025000
}
```

| Field        | Kiểu   | Mô tả                                                           |
| ------------ | ------ | --------------------------------------------------------------- |
| `event`      | string | Luôn `"record_done"`                                            |
| `pid`        | string | Pipeline ID                                                     |
| `sid`        | int    | Source index                                                    |
| `sname`      | string | Tên camera                                                      |
| `session_id` | int    | Session ID khớp với `record_started`                            |
| `width`      | int    | Video width từ `NvDsSRRecordingInfo::width`                     |
| `height`     | int    | Video height từ `NvDsSRRecordingInfo::height`                   |
| `filename`   | string | Full path tới file video từ `NvDsSRRecordingInfo::filename`     |
| `duration`   | int    | Raw `NvDsSRRecordingInfo::duration` (nanoseconds từ DeepStream) |
| `event_ts`   | int    | Unix epoch milliseconds tại thời điểm sr-done callback          |

**Reconnect hành vi** (nhật xử theo loại broker):

- **Redis** — Background thread retry với backoff exponential (5s initial → 60s max); nếu broker down, event bị drop + log
- **Kafka** — librdkafka tự retry với backoff exponential (5s initial → 60s max); message được queue mãi mãi (`message.timeout.ms=0`) và sẽ deliver khi broker trở lại online, ngay cả nếu broker down hàng giờ

Nếu `channel` rỗng, handler không publish gì.

### 3.1 Các struct chính

```cpp
// pipeline/include/engine/pipeline/probes/smart_record_probe_handler.hpp

struct RecordingSession {
    uint32_t    session_id   = 0;                    // ID từ nvurisrcbin start-sr
    uint32_t    source_id    = 0;
    std::string source_name;
    GstClockTime start_time  = GST_CLOCK_TIME_NONE;  // GstSystemClock time khi bắt đầu
    uint32_t    duration_sec = 0;                    // pre + post seconds
};

struct SourceRecordingState {
    GstClockTime last_record_time = GST_CLOCK_TIME_NONE; // Thời điểm actual_start của lần ghi gần nhất
    std::optional<RecordingSession> active_session;      // nullopt = không đang ghi
    gulong signal_handler_id = 0;                        // ID của sr-done signal connection
};
```

### 3.2 Class overview

```cpp
class SmartRecordProbeHandler {
public:
    void configure(const engine::core::config::PipelineConfig& config,
                   const engine::core::config::EventHandlerConfig& handler,
                   GstElement* multiuribin,
                   engine::core::messaging::IMessageProducer* producer);

    static GstPadProbeReturn on_buffer(GstPad* pad, GstPadProbeInfo* info, gpointer user_data);

private:
    GstPadProbeReturn process_batch(GstBuffer* buf, GstClockTime now);
    GstElement*       find_nvurisrcbin(uint32_t source_id);
    bool              can_start_recording(uint32_t source_id, GstClockTime now);
    void              start_recording(uint32_t source_id, uint64_t trigger_object_id, GstClockTime now);
    void              cleanup_expired_sessions(GstClockTime now);
    int               count_active_recordings() const;
    void              publish_record_started(uint32_t source_id, const std::string& source_name,
                                             uint32_t session_id, uint64_t trigger_object_id);
    void              publish_record_done(uint32_t source_id, const std::string& source_name,
                                          uint32_t session_id, NvDsSRRecordingInfo* info);
    static void       on_recording_done(GstElement*, gpointer, gpointer, gpointer user_data);
    void              disconnect_all_signals();

    // Config
    std::string pipeline_id_;
    std::vector<std::string> label_filter_;
    int pre_event_sec_  = 2;
    int post_event_sec_ = 20;
    int min_interval_sec_          = 2;
    int max_concurrent_recordings_ = 0;

    // Element refs
    GstElement* multiuribin_ = nullptr;                       // Borrowed (owned by pipeline)
    std::unordered_map<uint32_t, GstElement*> source_bins_;   // Cached nvurisrcbin per source

    // State
    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, SourceRecordingState> source_states_;
    std::unordered_map<int, std::string> source_id_to_name_;
    std::atomic<bool> shutting_down_{false};

    // Messaging
    engine::core::messaging::IMessageProducer* producer_ = nullptr;
};
```

---

## 4. Luồng Hoạt Động

### 4.1 Lifecycle: configure → on_buffer → sr-done

```
[ProbeHandlerManager]
    │
    ├─ new SmartRecordProbeHandler()
    ├─ handler->configure(config, handler_cfg, multiuribin, producer)
    └─ gst_pad_add_probe(tracker:src, BUFFER, on_buffer, handler, delete_handler)

[Every buffer on tracker:src]
    on_buffer()
    │   ↓ early return if shutting_down_
    ├─ get_gst_clock_now()               // GstSystemClock — one call per batch
    └─ process_batch(buf, now)
          ├─ cleanup_expired_sessions(now)
          └─ for each frame → for each obj:
                ├─ label_filter check
                └─ can_start_recording(source_id, now)?
                      └─ YES → start_recording(source_id, obj->object_id, now)
                                  ├─ find_nvurisrcbin(source_id)   // with stale-cache check
                                  ├─ g_signal_emit_by_name("start-sr", ...)
                                  ├─ update SourceRecordingState
                                  └─ publish_record_started(source_id, source_name, session_id, trigger_object_id)

[When recording finishes — on nvurisrcbin thread]
    on_recording_done(nvurisrcbin, recording_info, ...)
    ├─ early return if shutting_down_
    ├─ source_id from qdata (kSourceIdDataKey) hoặc linear search
    ├─ clear active_session
    └─ publish_record_done(source_id, source_name, session_id, NvDsSRRecordingInfo*)

[Destructor]
    shutting_down_.store(true)   // TRƯỚC KHI disconnect signals
    └─ disconnect_all_signals()
```

---

## 5. Design Decisions

### 5.1 GstClockTime thay vì std::chrono

**Vấn đề**: Dùng `std::chrono::steady_clock` cho interval tracking có thể drift so với GStreamer pipeline clock, gây ra timing không nhất quán.

**Giải pháp**: Dùng `gst_system_clock_obtain()` + `gst_clock_get_time()` — cùng clock domain với GStreamer pipeline.

```cpp
static inline GstClockTime get_gst_clock_now() {
    GstClock* clock = gst_system_clock_obtain();
    GstClockTime now = gst_clock_get_time(clock);
    gst_object_unref(clock);  // GstSystemClock là ref-counted singleton
    return now;
}
```

> **Lưu ý**: `gst_system_clock_obtain()` PHẢI được unref sau mỗi lần dùng — đây là singleton ref-counted của GStreamer.

Interval check dùng GstClockTime arithmetic (64-bit nanosecond):

```cpp
const GstClockTime min_interval_ns = static_cast<GstClockTime>(min_interval_sec_) * GST_SECOND;
if (now - state.last_record_time < min_interval_ns) return false;
```

### 5.2 actual_start — Interval Reference từ Đầu Recording Buffer

**Vấn đề**: Nếu interval reference là thời điểm detect (`now`), lần detect tiếp theo có thể trigger recording ngay sau khi recording hiện tại kết thúc — trong khi circular buffer của `pre_event_sec` giây trước detection vẫn đang được flush.

**Giải pháp**: Lưu `last_record_time = actual_start` trong đó:

```cpp
const GstClockTime pre_event_ns  = static_cast<GstClockTime>(pre_event_sec_) * GST_SECOND;
const GstClockTime actual_start  = (now > pre_event_ns) ? (now - pre_event_ns) : 0;

state.last_record_time = actual_start;  // Interval reference = đầu recording buffer
```

**Ý nghĩa**: `min_interval_sec` được tính từ đầu recording buffer, không phải từ thời điểm detection. Điều này đảm bảo recordings không bị overlap về nội dung.

**Trace ví dụ** (`pre=2, post=20, min_interval=2`):

```
T=10s  → Detect car
           actual_start = 10 - 2 = 8s
           last_record_time = 8s
           active_session  = { session_id=5, duration_sec=22 }
           g_signal_emit_by_name("start-sr", start=2, total=22)
           → video bắt đầu từ T=8s, kết thúc tại T=30s

T=12s  → Detect car lại
           can_start_recording? → active_session != nullopt → FALSE (đang ghi)

T=30s  → sr-done callback
           active_session = nullopt

T=32s  → Detect car lại
           can_start_recording?
             active_session  = nullopt     → OK
             32 - 8 = 24s >= 2s (min_interval) → OK
           → ALLOW: bắt đầu recording mới
```

**Trường hợp sr-done bị mất** (stream ngắt tại T=15s):

```
deadline = session.start_time + duration_sec * GST_SECOND + kSessionGracePeriodNs
         = T=10s + 22s + 5s = T=37s
T=37s  → cleanup_expired_sessions() → session expire → active_session = nullopt
T=38s  → source có thể ghi lại bình thường
```

### 5.3 Stale Cache Detection cho nvurisrcbin

**Vấn đề**: Sau khi hot-swap stream hoặc pipeline state change, cached `GstElement*` có thể trỏ tới element đã bị remove khỏi pipeline — dùng nó gây crash.

**Giải pháp**: Validate cache bằng `gst_bin_get_by_name()` mỗi lần lookup:

```cpp
GstElement* SmartRecordProbeHandler::find_nvurisrcbin(uint32_t source_id) {
    auto it = source_bins_.find(source_id);
    if (it != source_bins_.end() && it->second) {
        GstElement* cached = it->second;
        // Validate: element still in pipeline?
        GstElement* probe = gst_bin_get_by_name(GST_BIN(multiuribin_),
                                                  GST_ELEMENT_NAME(cached));
        if (probe) {
            const bool still_same = (probe == cached);
            gst_object_unref(probe);  // extra ref từ gst_bin_get_by_name
            if (still_same) return cached;  // Cache còn hợp lệ
        }
        // Stale → evict
        gst_object_unref(cached);
        source_bins_.erase(it);
    }
    // Re-discover từ nvmultiurisrcbin
    GstElement* fresh = find_nvurisrcbin_in_bin(multiuribin_, source_id);
    if (fresh) source_bins_[source_id] = fresh;  // Cache owns the ref
    return fresh;
}
```

### 5.4 nvurisrcbin Discovery Path

`nvmultiurisrcbin` tổ chức source elements theo cấu trúc:

```
nvmultiurisrcbin0
  └─ nvmultiurisrcbin0_creator  (sub-bin)
       ├─ dsnvurisrcbin0
       ├─ dsnvurisrcbin1
       └─ dsnvurisrcbin2
```

Handler tìm element theo path cố định này:

```cpp
// Bước 1: Tìm creator sub-bin
gchar* creator_name = g_strdup_printf("%s_creator", parent_name);
GstElement* creator_bin = gst_bin_get_by_name(GST_BIN(multiuribin), creator_name);

// Bước 2: Tìm "dsnvurisrcbin{source_id}" trong creator
gchar* target_name = g_strdup_printf("dsnvurisrcbin%u", source_id);
GstElement* urisrcbin = gst_bin_get_by_name(GST_BIN(creator_bin), target_name);

// Bước 3: Validate factory type
GstElementFactory* factory = gst_element_get_factory(urisrcbin);
const gchar* fname = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
assert(g_str_has_prefix(fname, "nvurisrcbin"));
```

> **DS8 Naming Convention**: Tên `dsnvurisrcbin{N}` và sub-bin `{parent}_creator` là convention của DeepStream 8.0 — có thể thay đổi ở các phiên bản khác.

### 5.5 cleanup_expired_sessions — Guard Cho Missed sr-done

**Vấn đề**: Nếu signal `sr-done` không được nhận (e.g., source bị ngắt kết nối, GStreamer error), `active_session` sẽ tồn tại mãi → source đó không bao giờ được ghi lại nữa do `can_start_recording()` luôn trả về `false`.

**Giải pháp**: Mỗi batch, `cleanup_expired_sessions()` kiểm tra deadline:

```cpp
void SmartRecordProbeHandler::cleanup_expired_sessions(GstClockTime now) {
    std::lock_guard lock(mutex_);
    for (auto& [source_id, state] : source_states_) {
        if (!state.active_session) continue;
        const auto& session = *state.active_session;

        const GstClockTime expected_ns = static_cast<GstClockTime>(session.duration_sec) * GST_SECOND;
        const GstClockTime deadline    = session.start_time + expected_ns + kSessionGracePeriodNs;
        //                             = start_time + (pre + post) + 5s grace

        if (now > deadline) {
            LOG_W("SmartRecord: session {} expired without sr-done — clearing", session.session_id);
            state.active_session.reset();
        }
    }
}
```

Hằng số: `kSessionGracePeriodNs = 5 * GST_SECOND` (5 giây grace period sau khi recording dự kiến kết thúc).

### 5.6 max_concurrent_recordings

Giới hạn số lượng recordings đồng thời trên toàn pipeline:

```cpp
bool SmartRecordProbeHandler::can_start_recording(uint32_t source_id, GstClockTime now) {
    std::lock_guard lock(mutex_);
    auto& state = source_states_[source_id];

    if (state.active_session) return false;  // Source này đang ghi

    if (max_concurrent_recordings_ > 0) {
        if (count_active_recordings() >= max_concurrent_recordings_) {
            return false;  // Đã đạt giới hạn toàn pipeline
        }
    }

    // Min-interval check...
    return true;
}

int SmartRecordProbeHandler::count_active_recordings() const {
    // Caller MUST hold mutex_
    int count = 0;
    for (const auto& [sid, state] : source_states_)
        if (state.active_session) ++count;
    return count;
}
```

`max_concurrent_recordings = 0` (default) = không giới hạn.

### 5.7 shutting*down* — Guard Teardown Race

**Vấn đề**: `on_recording_done` callback chạy trên GStreamer streaming thread — có thể fire sau khi `SmartRecordProbeHandler::~SmartRecordProbeHandler()` đã bắt đầu deallocate members.

**Giải pháp**: `std::atomic<bool> shutting_down_{false}` — set `true` **trước** khi disconnect signals:

```cpp
SmartRecordProbeHandler::~SmartRecordProbeHandler() {
    // 1. Signal teardown TRƯỚC TIÊN — callbacks sau đây sẽ early-return
    shutting_down_.store(true, std::memory_order_seq_cst);

    // 2. Sau đó mới disconnect signals
    disconnect_all_signals();

    // 3. Release cached refs
    for (auto& [sid, elem] : source_bins_)
        if (elem) gst_object_unref(elem);
    source_bins_.clear();
}
```

Callback check:

```cpp
void SmartRecordProbeHandler::on_recording_done(..., gpointer user_data) {
    auto* self = static_cast<SmartRecordProbeHandler*>(user_data);
    if (self->shutting_down_.load(std::memory_order_relaxed)) return;  // Safe early exit
    // ...
}
```

### 5.8 source_id → nvurisrcbin Mapping qua qdata

Khi connect signal `sr-done`, handler attach `source_id` vào element bằng GObject qdata:

```cpp
g_object_set_data(G_OBJECT(urisrcbin), kSourceIdDataKey, GUINT_TO_POINTER(source_id));
```

Trong `on_recording_done`, ưu tiên dùng qdata để tra cứu `source_id`:

```cpp
gpointer sid_ptr = g_object_get_data(G_OBJECT(nvurisrcbin), kSourceIdDataKey);
if (sid_ptr) {
    source_id = GPOINTER_TO_UINT(sid_ptr);
} else {
    // Fallback: linear search qua source_bins_ cache
}
```

Lý do: qdata vẫn tồn tại ngay cả khi cache `source_bins_` đã bị evicted (stale cache).

---

## 6. Sự Kiện Publish (JSON)

Khi `broker.channel` được cấu hình, handler publish 2 loại event qua `IMessageProducer::publish()`. Format field names tương thích với lantanav2 để consumer side có thể xử lý cả hai:

### 6.1 record_started

Được publish ngay sau khi `g_signal_emit_by_name("start-sr", ...)` thành công:

```json
{
  "event": "record_started",
  "pid": "pipeline-01",
  "sid": 0,
  "sname": "camera-01",
  "session_id": 42,
  "start_time": 5,
  "duration": 25000,
  "trigger_obj": 12345,
  "event_ts": 1741000000000
}
```

| Field         | Type   | Mô tả                                                        |
| ------------- | ------ | ------------------------------------------------------------ |
| `event`       | string | `"record_started"`                                           |
| `pid`         | string | `config.pipeline.id`                                         |
| `sid`         | uint32 | `frame_meta->source_id`                                      |
| `sname`       | string | `config.sources.cameras[source_id].id`                       |
| `session_id`  | uint32 | `sr_session_id` từ `start-sr` emit return                    |
| `start_time`  | uint32 | `pre_event_sec_` — độ sâu circular buffer back-fill (giây)   |
| `duration`    | uint32 | `(pre_event_sec + post_event_sec) * 1000` — tổng duration ms |
| `trigger_obj` | uint64 | `obj->object_id` — tracker ID của object trigger recording   |
| `event_ts`    | int64  | Unix epoch milliseconds (`now_epoch_ms()`)                   |

### 6.2 record_done

Được publish trong `on_recording_done` callback (chạy trên GStreamer thread), nhận `NvDsSRRecordingInfo*` từ DeepStream signal:

```json
{
  "event": "record_done",
  "pid": "pipeline-01",
  "sid": 0,
  "sname": "camera-01",
  "session_id": 42,
  "width": 1920,
  "height": 1080,
  "filename": "/opt/engine/data/rec/vms_rec_cam0_1741000000.mp4",
  "duration": 25000000000,
  "event_ts": 1741000025000
}
```

| Field        | Type   | Mô tả                                                                        |
| ------------ | ------ | ---------------------------------------------------------------------------- |
| `event`      | string | `"record_done"`                                                              |
| `pid`        | string | `config.pipeline.id`                                                         |
| `sid`        | uint32 | `source_id` tra từ qdata hoặc linear search                                  |
| `sname`      | string | `config.sources.cameras[source_id].id`                                       |
| `session_id` | uint32 | ID của session đã kết thúc — consumer match với `record_started`             |
| `width`      | uint32 | `NvDsSRRecordingInfo::width` — độ rộng video được ghi                        |
| `height`     | uint32 | `NvDsSRRecordingInfo::height` — chiều cao video được ghi                     |
| `filename`   | string | `NvDsSRRecordingInfo::filename` — đường dẫn file đầy đủ (hoặc `""` nếu null) |
| `duration`   | uint64 | `NvDsSRRecordingInfo::duration` — **nanoseconds** (GStreamer clock domain)   |
| `event_ts`   | int64  | Unix epoch milliseconds (`now_epoch_ms()`)                                   |

> **Duration units**: `record_started.duration` tính bằng **milliseconds**; `record_done.duration` tính bằng **nanoseconds** (trực tiếp từ `NvDsSRRecordingInfo::duration`). Để convert sang giây: `duration / 1_000_000_000.0`.

---

## 7. Thread Safety

| Resource             | Guard                     | Notes                                              |
| -------------------- | ------------------------- | -------------------------------------------------- |
| `source_states_`     | `mutex_`                  | Đọc/ghi từ probe thread và sr-done callback thread |
| `source_bins_` cache | Gọi từ probe thread only  | Không cần mutex vì chỉ probe thread access         |
| `source_id_to_name_` | Chỉ configure phase write | Read-only sau `configure()` — safe without lock    |
| `shutting_down_`     | `std::atomic<bool>`       | Lock-free flag cho callback threads                |

> **Probe thread**: `on_buffer` → `process_batch` → `can_start_recording` → `start_recording` — tất cả chạy trên cùng 1 thread GStreamer streaming.

> **sr-done thread**: `on_recording_done` — chạy trên internal thread của `nvurisrcbin`, khác với probe thread.

---

## 8. Tích Hợp với ProbeHandlerManager

`ProbeHandlerManager::attach_probes()` dispatch block cho smart_record:

```cpp
// pipeline/src/probes/probe_handler_manager.cpp
} else if (cfg.trigger == "smart_record") {
    GstElement* multiuribin = nullptr;
    if (!cfg.source_element.empty()) {
        multiuribin = find_element(cfg.source_element);
    }

    auto* handler = new SmartRecordProbeHandler();
    handler->configure(full_config_, cfg, multiuribin, producer_);

    gst_pad_add_probe(
        pad, GST_PAD_PROBE_TYPE_BUFFER,
        SmartRecordProbeHandler::on_buffer, handler,
        [](gpointer ud) { delete static_cast<SmartRecordProbeHandler*>(ud); });
}
```

**Ownership**: `ProbeHandlerManager` tạo handler bằng `new`, GStreamer nhận ownership qua `GDestroyNotify` (lambda gọi `delete`). Handler bị destroy khi probe bị remove (thường khi pipeline shutdown).

---

## 9. Điều Kiện Để SmartRecord Hoạt Động

Checklist đầy đủ để smart recording hoạt động:

| Điều Kiện                                  | Nơi Cấu Hình           | Ghi Chú                                                               |
| ------------------------------------------ | ---------------------- | --------------------------------------------------------------------- |
| `sources.smart_record > 0`                 | YAML `sources:`        | `2` = multi (recommended)                                             |
| `sources.smart_rec_dir_path` writable      | YAML + host filesystem | Thư mục phải tồn tại và có quyền ghi                                  |
| `source_element` trỏ đúng nvmultiurisrcbin | YAML `event_handlers:` | Tên element trong pipeline                                            |
| `enable: true` trong event_handler entry   | YAML `event_handlers:` |                                                                       |
| `label_filter` chứa labels từ PGIE model   | YAML `event_handlers:` | Rỗng = accept all                                                     |
| `libnvdsgst_smartrecord.so` available      | DeepStream install     | Có sẵn trong DS8 install tại `/opt/nvidia/deepstream/deepstream/lib/` |
| GStreamer thread safe: không blocking I/O  | Implementation         | Probe callback KHÔNG được block                                       |

---

## 10. Xử Lý Lỗi Thường Gặp

### "nvurisrcbin not found for source N"

```
[W] SmartRecord: nvurisrcbin not found for source 0
```

**Nguyên nhân**: `source_element` sai tên, hoặc `nvmultiurisrcbin` chưa có child element cho source đó (stream chưa kết nối).

**Giải pháp**: Kiểm tra `source_element` khớp với tên element trong pipeline. Có thể inspect bằng `GST_DEBUG_DUMP_DOT_DIR`.

### "max concurrent recordings reached"

```
[D] SmartRecord: max concurrent recordings (4) reached, blocking source 2
```

**Nguyên nhân**: `max_concurrent_recordings` đã đạt giới hạn. Source 2 sẽ được ghi khi một session khác kết thúc.

**Giải pháp**: Tăng `max_concurrent_recordings` hoặc giảm `post_event_sec`.

### Session expired without sr-done

```
[W] SmartRecord: session 13 (source 1) expired without sr-done (expected 25s + 5s grace) — clearing
```

**Nguyên nhân**: `sr-done` signal không được nhận. Thường xảy ra khi:

- Source bị disconnect trong khi đang ghi
- Pipeline error làm gián đoạn `nvurisrcbin`

**Hành vi**: Handler tự clear `active_session` sau `duration_sec + 5s` → source tiếp tục hoạt động bình thường.

### Stale cache warning

```
[W] SmartRecord: cached nvurisrcbin for source 0 is stale, re-discovering
```

**Nguyên nhân**: Source stream được hot-swap, tạo ra `nvurisrcbin` mới thay thế instance cũ.

**Hành vi**: Handler tự re-discover element mới — không cần can thiệp.

---

## 11. Quan Hệ với ClassIdNamespaceHandler

Khi pipeline sử dụng cả `class_id_offset`/`class_id_restore` và `smart_record`, **thứ tự trong `event_handlers:` rất quan trọng**:

```yaml
event_handlers:
  - id: class_id_offset
    probe_element: tracker
    pad_name: sink # sink pad — TRƯỚC khi tracker xử lý
    trigger: class_id_offset

  - id: class_id_restore
    probe_element: tracker
    pad_name: src # src pad — SAU tracker
    trigger: class_id_restore

  - id: smart_record # PHẢI đứng SAU class_id_restore
    probe_element: tracker
    pad_name: src
    trigger: smart_record
    label_filter: [car, person]
```

**Lý do**: GStreamer probe FIFO — `class_id_restore` phải chạy trước `smart_record` trên cùng `tracker:src` pad để `label_filter` nhận được class_ids gốc.

> 📖 Xem thêm: [`docs/architecture/probes/class_id_namespacing_handler.md`](class_id_namespacing_handler.md)

---

## 12. File References

| File                                                                     | Vai trò                                               |
| ------------------------------------------------------------------------ | ----------------------------------------------------- |
| `pipeline/include/engine/pipeline/probes/smart_record_probe_handler.hpp` | Header — structs, class declaration                   |
| `pipeline/src/probes/smart_record_probe_handler.cpp`                     | Implementation                                        |
| `pipeline/src/probes/probe_handler_manager.cpp`                          | Registration + dispatch (`trigger == "smart_record"`) |
| `core/include/engine/core/config/config_types.hpp`                       | `EventHandlerConfig` struct (smart_record fields)     |
| `infrastructure/config_parser/src/yaml_parser_handlers.cpp`              | YAML → `EventHandlerConfig` parsing                   |
| `docs/architecture/deepstream/07_event_handlers_probes.md`               | Overview của tất cả probe handlers                    |
| `docs/configs/deepstream_default.yml`                                    | Full YAML config ví dụ                                |

---

## 13. Logic Các Config Field (pre / post / min_interval)

### 13.1 Giá trị truyền vào DeepStream

Khi `start_recording()` được gọi, handler truyền 2 tham số vào GSignal `start-sr`:

```
start_time_sec = pre_event_sec          → DeepStream back-fill từ circular buffer
total_duration = pre_event_sec + post_event_sec  → tổng độ dài video
```

Với `pre=2, post=20`:

```
start-sr(start=2, total=22)
→ video = 2s trước detection + 20s sau detection = 22s
```

### 13.2 Trace đầy đủ với `pre=2, post=20, min_interval=2`

```
T=10s  ─ Detect car
           actual_start = 10 - 2 = 8s          (đầu circular buffer)
           last_record_time = 8s
           active_session  = { session_id=5, duration_sec=22 }
           g_signal_emit_by_name("start-sr", start=2, total=22)
           publish record_started { duration=22000ms, start_time=2, trigger_obj=... }
           → file ghi từ T=8s → T=30s

T=12s  ─ Detect car lại
           can_start_recording?
           active_session != nullopt → FALSE (đang ghi, chặn ngay)

T=30s  ─ sr-done callback fire
           active_session = nullopt
           publish record_done { filename="...", duration=22000000000ns, width=1920, ... }

T=32s  ─ Detect car lại
           can_start_recording?
             active_session  = nullopt          ✓
             32 - 8 = 24s  ≥ 2s (min_interval) ✓
           → ALLOW: bắt đầu recording mới
```

### 13.3 Trường hợp sr-done bị mất (stream ngắt)

```
T=10s  ─ Detect → active_session bắt đầu, duration_sec=22
T=15s  ─ Stream bị ngắt, sr-done không bao giờ đến

deadline = session.start_time + 22*GST_SECOND + kSessionGracePeriodNs (5s)
         = T=10s + 22s + 5s = T=37s

T=37s  ─ cleanup_expired_sessions() chạy trong buffer tiếp theo
           → session expire → active_session = nullopt
T=38s  ─ source có thể ghi lại bình thường
```

### 13.4 Tóm tắt ý nghĩa từng field

| Field              | Ảnh hưởng đến                               | Ghi chú                                                                 |
| ------------------ | ------------------------------------------- | ----------------------------------------------------------------------- |
| `pre_event_sec`    | Nội dung trước detection trong video output | Phụ thuộc vào `smart_rec_cache` ≥ `pre_event_sec` trên nvmultiurisrcbin |
| `post_event_sec`   | Nội dung sau detection trong video output   | Video dài = `pre + post` giây                                           |
| `min_interval_sec` | Khoảng cách tối thiểu giữa 2 buffer bắt đầu | Tính từ `actual_start` (đầu buffer), không phải thời điểm detect        |

> ⚠️ `smart_rec_cache` trên `nvmultiurisrcbin` phải ≥ `pre_event_sec`. Nếu cache nhỏ hơn, DeepStream chỉ back-fill được phần cache có sẵn.

---

## 14. So Sánh với lantanav2

### 14.1 Bảng so sánh

| Khía cạnh               | lantanav2                                                        | vms-engine                                                                  |
| ----------------------- | ---------------------------------------------------------------- | --------------------------------------------------------------------------- |
| **Messaging**           | `RedisStreamProducer` hardcode                                   | `IMessageProducer*` interface — pluggable                                   |
| **Config API**          | `init_context(string, std::any)` — parse thủ công                | `configure(PipelineConfig&, ...)` — type-safe                               |
| **Callback typing**     | `on_recording_done(GstElement*, NvDsSRRecordingInfo* info, ...)` | `on_recording_done(GstElement*, gpointer, ...)` + cast nội bộ               |
| **Mutex**               | 2 mutex: `elements_mutex_` + `state_mutex_`                      | 1 mutex duy nhất — ít risk deadlock hơn                                     |
| **Element cache**       | `GstElementRef` RAII class (~50 dòng)                            | Raw `GstElement*` + stale check bằng `gst_bin_get_by_name`                  |
| **Per-object debounce** | `object_last_seen` map per source_id                             | Bỏ — session-level guard đủ, ít memory hơn                                  |
| **Clock**               | Kết hợp `std::chrono` + `GstClockTime`                           | Thuần `GstClockTime` — nhất quán 1 clock domain                             |
| **Publish methods**     | Inline trong `start_recording` / `on_recording_done`             | Tách `publish_record_started` + `publish_record_done` — có Doxygen, dễ test |
| **Publish format**      | Redis XADD flat key-value                                        | JSON qua `IMessageProducer::publish(channel, json)`                         |

### 14.2 Publish field names — tương thích 2 chiều

Field names được giữ giống lantanav2 để consumer side xử lý được cả hai:

| Field           | lantanav2 key    | vms-engine key   | Ghi chú                                                                  |
| --------------- | ---------------- | ---------------- | ------------------------------------------------------------------------ |
| pipeline id     | `pid`            | `pid`            | ✅ giống nhau                                                            |
| source id       | `sid`            | `sid`            | ✅ giống nhau                                                            |
| source name     | `sname`          | `sname`          | ✅ giống nhau                                                            |
| session id      | `session_id`     | `session_id`     | ✅ giống nhau                                                            |
| event time      | `event_ts`       | `event_ts`       | ✅ giống nhau — Unix epoch ms                                            |
| pre-event sec   | `start_time`     | `start_time`     | ✅ giống nhau — giây                                                     |
| total ms        | `duration` (ms)  | `duration` (ms)  | ✅ giống nhau trong `record_started`                                     |
| trigger obj     | `trigger_obj`    | `trigger_obj`    | ✅ giống nhau                                                            |
| file path       | `filename`       | `filename`       | ✅ giống nhau                                                            |
| video dims      | `width`/`height` | `width`/`height` | ✅ giống nhau                                                            |
| actual duration | `duration` (ns)  | `duration` (ns)  | ✅ giống nhau trong `record_done` — nanoseconds từ `NvDsSRRecordingInfo` |

### 14.3 Cải thiện chính của vms-engine

**1. `IMessageProducer` thay vì hardcode Redis**

lantanav2 chỉ dùng được Redis. vms-engine inject interface — cùng handler chạy được với Redis, Kafka, hoặc bất kỳ broker nào mà không sửa code.

**2. Config type-safe**

lantanav2 parse chuỗi `"pre=5;post=20;redis_host=..."` bằng `istringstream` — lỗi config chỉ phát hiện lúc runtime. vms-engine nhận `EventHandlerConfig` struct đã parse từ YAML — lỗi phát hiện lúc parse.

**3. Một clock domain**

lantanav2 lưu `wall_clock_start = std::chrono::system_clock::now()` bên cạnh `GstClockTime start_time` trong session — 2 clock song song có thể drift. vms-engine chỉ dùng `GstClockTime` cho timing, `now_epoch_ms()` chỉ khi publish JSON.

**4. Bỏ `object_last_seen` per-object map**

lantanav2 track từng `object_id` riêng với `GstClockTime last_seen`. vms-engine bỏ map này — `active_session` + `min_interval` ở session level đã đủ ngăn overlap, ít memory hơn với nhiều object.

**5. Publish methods tách biệt**

lantanav2 publish inline. vms-engine tách thành `publish_record_started()` + `publish_record_done()` — mỗi method độc lập, có Doxygen, có thể unit test riêng.
