# VMS Engine — RAII Reference Guide

> **RAII = Resource Acquisition Is Initialization**  
> Bind a resource's lifetime to a C++ stack object.  
> The destructor (`~T()`) runs unconditionally on scope exit — normal `return`, early `return`, or unwind from an exception.  
> This makes cleanup deterministic and eliminates an entire class of leaks.

---

## Table of Contents

- [Why RAII?](#1-why-raii)
- [Smart Pointers — Memory (Heap)](#2-smart-pointers--memory-heap)
- [File Handles / Sockets](#3-file-handles--sockets)
- [Mutex / Locks (Multi-threading)](#4-mutex--locks-multi-threading)
- [Timers / Profiling Scopes](#5-timers--profiling-scopes)
- [Scope Guard / Scope Exit](#6-scope-guard--scope-exit)
- [GStreamer & GLib Resources](#7-gstreamer--glib-resources)
- [NvDs Metadata — DO NOT WRAP](#8-nvds-metadata--do-not-wrap)
- [Custom RAII Class with Destructor](#9-custom-raii-class-with-destructor)
- [Choosing the Right RAII Tool](#10-choosing-the-right-raii-tool)
- [Anti-patterns to Avoid](#11-anti-patterns-to-avoid)
- [Rule of Five — Move Semantics](#12-rule-of-five--move-semantics)
- [GPU / CUDA Resources (DeepStream)](#13-gpu--cuda-resources-deepstream)
- [RAII in Containers](#14-raii-in-containers)
- [`[[nodiscard]]` — Enforce RAII Usage](#15-nodiscard--enforce-raii-usage)
- [Exception Safety Guarantees](#16-exception-safety-guarantees)

---

## 1. Why RAII?

```
Three guarantees that RAII provides:

✅ Auto-release      ~T() runs on scope exit — no unref/free/close at each exit point
✅ Exception-safe    stack unwinding calls all destructors even when exception propagates
✅ No leak by default lifecycle encoded once in ~T(), not scattered across every branch
```

**Side-by-side — exception safety:**

```cpp
// ❌ WITHOUT RAII: caps leaks when exception propagates out
void bad() {
    GstCaps* caps = gst_caps_new_simple("video/x-raw", nullptr);
    some_op_that_may_throw();   // exception fires → next line skipped → LEAK
    gst_caps_unref(caps);
}

// ✅ WITH RAII: destructor guaranteed regardless of how scope exits
void good() {
    engine::core::utils::GstCapsPtr caps(
        gst_caps_new_simple("video/x-raw", nullptr), gst_caps_unref);
    some_op_that_may_throw();   // exception fires → ~GstCapsPtr → gst_caps_unref ✅
}
```

**Multiple early returns — no manual cleanup at each exit:**

```cpp
bool init_pipeline() {
    auto src = engine::core::utils::make_gst_element("nvmultiurisrcbin", "src");
    if (!src)  return false;   // ~GstElementPtr auto-cleans

    auto mux = engine::core::utils::make_gst_element("nvstreammux", "mux");
    if (!mux)  return false;   // both src and mux cleaned automatically

    if (!gst_bin_add(GST_BIN(pipeline_), src.get())) return false;
    src.release();             // bin owns now — disarm guard
    if (!gst_bin_add(GST_BIN(pipeline_), mux.get())) return false;
    mux.release();
    return true;
}
```

C++ has **no garbage collector**. RAII is the primary mechanism that ensures deterministic resource cleanup.

---

## 2. Smart Pointers — Memory (Heap)

### `std::unique_ptr` — sole ownership

```cpp
#include <memory>

// Basic heap allocation
auto config = std::make_unique<PipelineConfig>();  // auto-deleted on scope exit
config->version = "2.0";

// Array
auto buffer = std::make_unique<uint8_t[]>(frame_size);

// Custom deleter for C API resources
std::unique_ptr<FILE, decltype(&fclose)> file(
    fopen("pipeline.log", "w"), &fclose);
if (!file) { LOG_E("Cannot open log file"); return; }
fprintf(file.get(), "Pipeline started\n");
// fclose() called automatically
```

### `std::shared_ptr` — shared ownership

```cpp
// Use when multiple owners exist (e.g. builder factory shared across phases)
auto factory = std::make_shared<BuilderFactory>();

// Pass to multiple builders — all share ownership, resource deleted when last ref dies
SourceBuilder    src_builder(pipeline_, factory, &config, link_manager_);
ProcessingBuilder proc_builder(pipeline_, factory, &config, link_manager_, ...);
```

### `std::weak_ptr` — non-owning observer

```cpp
// Avoids circular reference (e.g. child holds ref to parent, parent holds child)
std::weak_ptr<HandlerManager> manager_ref_;  // does NOT prevent destruction

void use() {
    if (auto mgr = manager_ref_.lock()) {  // lock() returns shared_ptr or nullptr
        mgr->shutdown_all();
    }
}
```

### Smart pointer ownership table

| Pointer         | Ownership           | Copy | Move | Use when                             |
|-----------------|---------------------|------|------|--------------------------------------|
| `unique_ptr`    | Sole, exclusive     | ❌   | ✅   | One owner; no shared state needed    |
| `shared_ptr`    | Shared, ref-counted | ✅   | ✅   | Multiple owners; longer lifetimes    |
| `weak_ptr`      | Non-owning observer | ✅   | ✅   | Cache, back-pointer, break cycles    |
| `unique_ptr` + deleter | Custom release | ❌  | ✅   | C API handles (GstCaps, FILE, fd...) |

---

## 3. File Handles / Sockets

### File handle via `unique_ptr`

```cpp
#include <cstdio>
#include <memory>

// fclose is the deleter
std::unique_ptr<FILE, decltype(&fclose)> fp(
    fopen("/opt/engine/data/rec/metadata.json", "w"), &fclose);
if (!fp) {
    LOG_E("Failed to open metadata file");
    return false;
}
std::fputs(R"({"event":"start"})", fp.get());
// fclose auto-called at scope exit
```

### Custom RAII for file descriptor (POSIX `open`/`close`)

```cpp
#include <fcntl.h>
#include <unistd.h>

class FileDescriptorGuard {
public:
    explicit FileDescriptorGuard(int fd) noexcept : fd_(fd) {}
    ~FileDescriptorGuard() { if (fd_ >= 0) ::close(fd_); }

    FileDescriptorGuard(const FileDescriptorGuard&)            = delete;
    FileDescriptorGuard& operator=(const FileDescriptorGuard&) = delete;
    FileDescriptorGuard(FileDescriptorGuard&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }

    int get() const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ >= 0; }

private:
    int fd_ = -1;
};

// Usage
FileDescriptorGuard fd(::open("/dev/video0", O_RDONLY));
if (!fd.valid()) { LOG_E("Cannot open device"); return; }
// ::close() called automatically
```

### Custom RAII for TCP socket

```cpp
#include <sys/socket.h>

class SocketGuard {
public:
    explicit SocketGuard(int domain, int type, int protocol)
        : sock_(::socket(domain, type, protocol)) {}
    ~SocketGuard() { if (sock_ >= 0) ::close(sock_); }

    SocketGuard(const SocketGuard&)            = delete;
    SocketGuard& operator=(const SocketGuard&) = delete;
    SocketGuard(SocketGuard&& o) noexcept : sock_(o.sock_) { o.sock_ = -1; }

    int get() const noexcept { return sock_; }
    bool valid() const noexcept { return sock_ >= 0; }

private:
    int sock_ = -1;
};

// Usage in REST API adapter
SocketGuard sock(AF_INET, SOCK_STREAM, 0);
if (!sock.valid()) { LOG_E("socket() failed"); return; }
// ::close() called automatically
```

---

## 4. Mutex / Locks (Multi-threading)

Multiprocessing in vms-engine: GStreamer runs pad probes and signal handlers on **different threads**.
Always protect shared state with a mutex. Use RAII locks to avoid forgetting to unlock.

### `std::lock_guard` — simple scoped lock

```cpp
#include <mutex>

class HandlerManager {
    std::mutex registry_mutex_;
    std::unordered_map<std::string, GstElement*> element_registry_;
public:

    void register_element(const std::string& id, GstElement* element) {
        std::lock_guard<std::mutex> lock(registry_mutex_);
        // locked here
        element_registry_[id] = element;
    }  // ~lock_guard → mutex.unlock() ← even if exception thrown inside
};
```

### `std::unique_lock` — conditional wait / manual control

```cpp
#include <mutex>
#include <condition_variable>

class EventQueue {
    std::mutex mtx_;
    std::condition_variable cv_;
    std::vector<std::string> events_;
public:

    void push(std::string event) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            events_.push_back(std::move(event));
        }
        cv_.notify_one();
    }

    std::string pop() {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this]{ return !events_.empty(); });
        std::string e = std::move(events_.front());
        events_.erase(events_.begin());
        return e;  // unlock on return
    }
};
```

### `std::scoped_lock` — lock multiple mutexes deadlock-safe (C++17)

```cpp
#include <mutex>

std::mutex mtx_a, mtx_b;

void safe_transfer() {
    std::scoped_lock lock(mtx_a, mtx_b);  // acquires both atomically (no deadlock)
    // ...
}  // unlocks both
```

### Lock type comparison

| Type              | Copy/Move | Condition wait | Manual unlock | Use when                          |
|-------------------|-----------|----------------|---------------|-----------------------------------|
| `lock_guard`      | ❌        | ❌             | ❌            | Simple scope protection           |
| `unique_lock`     | ❌ / ✅   | ✅             | ✅            | `condition_variable`, deferred    |
| `scoped_lock`     | ❌        | ❌             | ❌            | Multiple mutexes, deadlock-safe   |
| `shared_lock`     | ❌ / ✅   | ❌             | ✅            | Read-many / write-one (`shared_mutex`) |

---

## 5. Timers / Profiling Scopes

Useful in hot paths (pad probes, inference callbacks) to measure latency without forgetting to record the end time.

### Scoped timer — logs duration on scope exit

```cpp
#include <chrono>
#include "engine/core/utils/logger.hpp"

class ScopedTimer {
public:
    explicit ScopedTimer(const char* label)
        : label_(label), start_(std::chrono::steady_clock::now()) {}

    ~ScopedTimer() {
        auto end = std::chrono::steady_clock::now();
        auto us  = std::chrono::duration_cast<std::chrono::microseconds>(end - start_).count();
        LOG_D("[PERF] {}: {} µs", label_, us);
    }

    // Non-copyable, non-movable (tied to this scope)
    ScopedTimer(const ScopedTimer&)            = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    const char* label_;
    std::chrono::steady_clock::time_point start_;
};

// Usage in pad probe callback
GstPadProbeReturn my_probe(GstPad*, GstPadProbeInfo* info, void*) {
    ScopedTimer t("inference_probe");
    // ... process NvDsBatchMeta ...
    return GST_PAD_PROBE_OK;
}  // ~ScopedTimer prints duration automatically
```

### Scoped counter — tracks in-flight operations

```cpp
#include <atomic>

class ScopedCounter {
public:
    explicit ScopedCounter(std::atomic<int>& counter) : counter_(counter) {
        ++counter_;
    }
    ~ScopedCounter() { --counter_; }

    ScopedCounter(const ScopedCounter&)            = delete;
    ScopedCounter& operator=(const ScopedCounter&) = delete;

private:
    std::atomic<int>& counter_;
};

// Usage
std::atomic<int> active_handlers_{0};

void handle_event(const NvDsEventMsgMeta* meta) {
    ScopedCounter tracker(active_handlers_);  // active_handlers_ incremented
    publish_to_redis(meta);
}  // active_handlers_ decremented automatically
```

---

## 6. Scope Guard / Scope Exit

A **scope guard** runs arbitrary cleanup code when a scope exits, without writing a dedicated RAII class.

### Manual scope guard (C++17 — no dependency)

```cpp
template<typename F>
class ScopeExit {
public:
    explicit ScopeExit(F f) : f_(std::move(f)) {}
    ~ScopeExit() { f_(); }

    ScopeExit(const ScopeExit&)            = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

private:
    F f_;
};

// Deduction guide (C++17)
template<typename F>
ScopeExit(F) -> ScopeExit<F>;

// Helper macro
#define SCOPE_EXIT(code) \
    auto _scope_exit_##__LINE__ = ScopeExit([&](){ code; })

// Usage
void init_gst_resources() {
    gst_init(nullptr, nullptr);
    SCOPE_EXIT(gst_deinit());   // always called even on early return

    auto pipeline = gst_pipeline_new("test");
    if (!pipeline) return;      // gst_deinit() still runs

    // ...
}  // gst_deinit() called here
```

### Conditional scope guard (dismiss on success)

```cpp
template<typename F>
class ScopeGuard {
public:
    explicit ScopeGuard(F f) : f_(std::move(f)), active_(true) {}
    ~ScopeGuard() { if (active_) f_(); }
    void dismiss() noexcept { active_ = false; }

    ScopeGuard(const ScopeGuard&)            = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

private:
    F    f_;
    bool active_;
};

template<typename F>
ScopeGuard(F) -> ScopeGuard<F>;

// Usage — rollback on failure, dismiss on success
bool configure_pipeline() {
    gst_init(nullptr, nullptr);
    ScopeGuard rollback([](){ gst_deinit(); });

    auto pipeline = gst_pipeline_new("main");
    if (!configure_elements(pipeline)) return false;  // gst_deinit runs

    rollback.dismiss();  // success — do NOT run gst_deinit here
    return true;
}
```

---

## 7. GStreamer & GLib Resources

### Ownership Rules

| Object | Obtained via | Release via | Notes |
|---|---|---|---|
| `GstElement*` (not in bin) | `gst_element_factory_make()` | `gst_object_unref()` | Caller owns until `gst_bin_add()` |
| `GstElement*` in bin | `gst_bin_add(bin, elem)` | *(bin owns)* | **Do NOT unref after add** |
| `GstPad*` (static) | `gst_element_get_static_pad()` | `gst_object_unref()` | Must unref even read-only |
| `GstPad*` (request) | `gst_element_get_request_pad()` | `gst_element_release_request_pad()` + `gst_object_unref()` | Release THEN unref |
| `GstCaps*` | `gst_caps_new_*()`, `gst_caps_copy()` | `gst_caps_unref()` | Reference-counted |
| `GstBus*` | `gst_pipeline_get_bus()` | `gst_object_unref()` | |
| `GMainLoop*` | `g_main_loop_new()` | `g_main_loop_unref()` | |
| `GError*` | set by GStreamer (out param) | `g_error_free()` | Check non-null before freeing |
| `gchar*` | `g_object_get()`, `g_strdup()` | `g_free()` | GLib heap allocation |

### `gst_utils.hpp` — RAII type aliases

File: `core/include/engine/core/utils/gst_utils.hpp`

```cpp
#pragma once
#include <gst/gst.h>
#include <glib.h>
#include <memory>

namespace engine::core::utils {

/// RAII guard for GstElement NOT yet added to a bin.
/// Call release() after gst_bin_add() to transfer ownership.
using GstElementPtr = std::unique_ptr<GstElement, decltype(&gst_object_unref)>;
inline GstElementPtr make_gst_element(const char* factory, const char* name) {
    return GstElementPtr(gst_element_factory_make(factory, name), gst_object_unref);
}

using GstCapsPtr   = std::unique_ptr<GstCaps,   decltype(&gst_caps_unref)>;
using GstPadPtr    = std::unique_ptr<GstPad,    decltype(&gst_object_unref)>;
using GstBusPtr    = std::unique_ptr<GstBus,    decltype(&gst_object_unref)>;
using GMainLoopPtr = std::unique_ptr<GMainLoop, decltype(&g_main_loop_unref)>;
using GErrorPtr    = std::unique_ptr<GError,    decltype(&g_error_free)>;
using GCharPtr     = std::unique_ptr<gchar,     decltype(&g_free)>;

} // namespace engine::core::utils
```

### Builder error-path pattern

```cpp
// pipeline/src/builders/infer_builder.cpp
#include "engine/core/utils/gst_utils.hpp"

GstElement* InferBuilder::build(const engine::core::config::PipelineConfig& config, int index) {
    const auto& elem = config.processing.elements[index];

    // RAII guard — auto-unref on any early return before gst_bin_add()
    auto infer = engine::core::utils::make_gst_element("nvinfer", elem.id.c_str());
    if (!infer) {
        LOG_E("Failed to create nvinfer '{}'", elem.id);
        return nullptr;  // ~GstElementPtr → gst_object_unref automatically
    }

    if (elem.config_file) {
        g_object_set(G_OBJECT(infer.get()),
            "config-file-path", elem.config_file->c_str(), nullptr);
    }

    if (!gst_bin_add(GST_BIN(bin_), infer.get())) {
        LOG_E("Failed to add nvinfer '{}' to bin", elem.id);
        return nullptr;  // guard still active — auto-unref
    }

    return infer.release();  // bin owns now — disarm guard
}
```

### Pad probe pattern

```cpp
void attach_inference_probe(GstElement* infer_element) {
    engine::core::utils::GstPadPtr pad(
        gst_element_get_static_pad(infer_element, "src"),
        gst_object_unref);
    if (!pad) { LOG_E("src pad not found on nvinfer"); return; }

    gst_pad_add_probe(pad.get(), GST_PAD_PROBE_TYPE_BUFFER,
                      inference_probe_callback, nullptr, nullptr);
    // gst_object_unref called automatically at end of scope
}
```

### Common usages

```cpp
// Caps
engine::core::utils::GstCapsPtr caps(
    gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "NV12", nullptr),
    gst_caps_unref);
g_object_set(G_OBJECT(filter), "caps", caps.get(), nullptr);

// gchar* string from g_object_get
gchar* raw_uri = nullptr;
g_object_get(G_OBJECT(src), "uri", &raw_uri, nullptr);
engine::core::utils::GCharPtr uri(raw_uri, g_free);
LOG_I("Source URI: {}", uri.get());

// GMainLoop lifetime
engine::core::utils::GMainLoopPtr loop(
    g_main_loop_new(nullptr, FALSE), g_main_loop_unref);
g_main_loop_run(loop.get());
// g_main_loop_unref called automatically
```

---

## 8. NvDs Metadata — DO NOT WRAP

NvDs metadata structs are **not ref-counted** and are owned by the GstBuffer / pipeline internals.
**Never** wrap them in a smart pointer or call free on them.

```cpp
// ✅ CORRECT — raw pointer, no cleanup
GstPadProbeReturn probe_cb(GstPad*, GstPadProbeInfo* info, void*) {
    GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
    NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buf);  // ← DO NOT FREE

    for (NvDsList* fl = batch_meta->frame_meta_list; fl; fl = fl->next) {
        auto* frame_meta = static_cast<NvDsFrameMeta*>(fl->data);   // ← DO NOT FREE

        for (NvDsList* ol = frame_meta->obj_meta_list; ol; ol = ol->next) {
            auto* obj_meta = static_cast<NvDsObjectMeta*>(ol->data);  // ← DO NOT FREE
            LOG_D("Detected: class={} conf={:.2f}", obj_meta->class_id, obj_meta->confidence);
        }
    }
    return GST_PAD_PROBE_OK;
}

// ❌ WRONG — will corrupt pipeline memory
auto* batch_meta_ptr = std::unique_ptr<NvDsBatchMeta>(batch_meta);  // CRASH
```

---

## 9. Custom RAII Class with Destructor

Use a **full class** (not just a `unique_ptr` alias) when cleanup requires **multiple ordered steps**.

### When to write a class

| Scenario | `unique_ptr` alias sufficient? |
|---|---|
| Single function call (`gst_object_unref`) | ✅ Yes |
| Two ordered steps (remove_watch → unref) | ❌ Write a class |
| Complex teardown (set_state(NULL) → unref) | ❌ Write a class |
| Need to track additional state during teardown | ❌ Write a class |

### GstBusGuard — two-step cleanup

```cpp
// GstBus: must remove watch FIRST, then unref
class GstBusGuard {
public:
    explicit GstBusGuard(GstElement* pipeline)
        : bus_(gst_pipeline_get_bus(GST_PIPELINE(pipeline))) {}

    ~GstBusGuard() {
        if (bus_) {
            gst_bus_remove_watch(bus_);  // step 1: detach message watch
            gst_object_unref(bus_);      // step 2: release ref
        }
    }

    // Non-copyable (unique ownership)
    GstBusGuard(const GstBusGuard&)            = delete;
    GstBusGuard& operator=(const GstBusGuard&) = delete;

    // Movable (transfer ownership)
    GstBusGuard(GstBusGuard&& o) noexcept : bus_(o.bus_) { o.bus_ = nullptr; }
    GstBusGuard& operator=(GstBusGuard&& o) noexcept {
        if (this != &o) { this->~GstBusGuard(); bus_ = o.bus_; o.bus_ = nullptr; }
        return *this;
    }

    GstBus* get() const noexcept { return bus_; }

private:
    GstBus* bus_ = nullptr;
};
```

### GstPipelineOwner — drain then unref

```cpp
// Top-level GstPipeline: must set NULL state THEN unref so all elements flush
class GstPipelineOwner {
public:
    explicit GstPipelineOwner(const std::string& name)
        : pipeline_(gst_pipeline_new(name.c_str())) {}

    ~GstPipelineOwner() {
        if (pipeline_) {
            gst_element_set_state(pipeline_, GST_STATE_NULL);  // step 1: drain
            gst_object_unref(pipeline_);                        // step 2: release
        }
    }

    GstPipelineOwner(const GstPipelineOwner&)            = delete;
    GstPipelineOwner& operator=(const GstPipelineOwner&) = delete;

    GstPipelineOwner(GstPipelineOwner&& o) noexcept : pipeline_(o.pipeline_) {
        o.pipeline_ = nullptr;
    }

    GstElement* get() const noexcept { return pipeline_; }

private:
    GstElement* pipeline_ = nullptr;
};
```

### Class design checklist

When writing a custom RAII class, always:

- [ ] `= delete` copy constructor and copy assignment (ownership is unique)
- [ ] Provide move constructor + move assignment if the object might be stored in containers
- [ ] In move constructor: `other.resource_ = nullptr / -1 / {invalid}` to prevent double-free
- [ ] In destructor: check for valid state before releasing (`if (resource_)`)
- [ ] Mark destructor logic as `noexcept` (destructors should never throw)
- [ ] Provide `get()` accessor (returns raw pointer/handle) — never return by value

---

## 10. Choosing the Right RAII Tool

| Scenario | Tool |
|---|---|
| Heap allocation (single owner) | `std::make_unique<T>()` |
| Heap allocation (shared between components) | `std::make_shared<T>()` |
| C API single-step cleanup (unref, fclose, g_free) | `unique_ptr` with custom deleter |
| C API multi-step cleanup (ordered steps) | Custom class with `~Destructor()` |
| File handle (POSIX `open`) | `FileDescriptorGuard` |
| TCP socket | `SocketGuard` |
| Mutex protection | `std::lock_guard` / `std::scoped_lock` |
| Conditional wait on mutex | `std::unique_lock` + `condition_variable` |
| Profiling / timing a scope | `ScopedTimer` |
| Arbitrary cleanup at scope exit | `ScopeExit` / `ScopeGuard` |
| GstElement (before bin add) | `GstElementPtr` + `make_gst_element()` |
| GstPad, GstCaps, GstBus, GMainLoop | Type alias (`GstPadPtr`, `GstCapsPtr`, …) |
| GstBus (with watch attached) | `GstBusGuard` |
| Top-level GstPipeline | `GstPipelineOwner` |
| NvDs metadata (batch/frame/obj) | **Raw pointer — never wrap, never free** |

---

## 11. Anti-patterns to Avoid

```cpp
// ❌ Manual multiple-exit cleanup (classic C-style, error-prone)
GstElement* elem = gst_element_factory_make("nvinfer", "pgie");
if (!elem)           { return nullptr; }                 // ← no leak here, ok
if (!configure(elem)){ gst_object_unref(elem); return nullptr; }  // ← easy to forget
if (!add(elem))      { gst_object_unref(elem); return nullptr; }  // ← if you add an exit, forget to add unref...
return elem;

// ✅ RAII eliminates all manual cleanup branches
auto elem = engine::core::utils::make_gst_element("nvinfer", "pgie");
if (!elem)           return nullptr;   // auto-unref
if (!configure(elem.get())) return nullptr;   // auto-unref
if (!gst_bin_add(GST_BIN(bin_), elem.get())) return nullptr;  // auto-unref
return elem.release();  // success: hand off ownership

// ❌ Forgetting to release() after bin_add (double-free)
auto elem = make_gst_element("queue", "q");
gst_bin_add(GST_BIN(bin_), elem.get());
return elem.get();  // ← scope end: ~GstElementPtr unrefs, but bin also owns → CRASH

// ✅ Always release() after successful bin_add
gst_bin_add(GST_BIN(bin_), elem.get());
return elem.release();  // bin takes ownership

// ❌ Wrapping NvDs metadata (pipeline owns it, do NOT free)
auto* batch = gst_buffer_get_nvds_batch_meta(buffer);
auto guard = std::unique_ptr<NvDsBatchMeta>(batch);  // CRASH when ~unique_ptr runs

// ❌ Raw mutex (easy to forget unlock; leaks lock on exception)
mtx_.lock();
do_work();   // if this throws, mutex stays locked forever
mtx_.unlock();

// ✅ Always use lock_guard
std::lock_guard<std::mutex> lock(mtx_);
do_work();   // exception safe — ~lock_guard → unlock
```

---

## 12. Rule of Five — Move Semantics

Any class that manages a resource (file, socket, GstElement, CUDA memory) must explicitly define all five special member functions — or explicitly `= delete`/`= default` them. Forgetting any one leads to **double-free**, **use-after-free**, or **resource leaks**.

### The Five Functions

| Function | Purpose |
|---|---|
| Destructor `~T()` | Release the resource |
| Copy constructor `T(const T&)` | Deep-copy or `= delete` |
| Copy assignment `T& operator=(const T&)` | Deep-copy or `= delete` |
| Move constructor `T(T&&) noexcept` | Transfer ownership from rvalue |
| Move assignment `T& operator=(T&&) noexcept` | Transfer ownership from rvalue |

### Full example — RAII class with Rule of Five

```cpp
class GpuBufferOwner {
public:
    explicit GpuBufferOwner(std::size_t bytes)
        : size_(bytes), ptr_(nullptr) {
        cudaError_t err = cudaMalloc(&ptr_, bytes);
        if (err != cudaSuccess || !ptr_) {
            throw std::runtime_error("cudaMalloc failed");
        }
    }

    // ① Destructor — release resource
    ~GpuBufferOwner() noexcept {
        if (ptr_) {
            cudaFree(ptr_);
        }
    }

    // ② Copy constructor — DELETED (GPU buffers are not trivially copyable)
    GpuBufferOwner(const GpuBufferOwner&)            = delete;

    // ③ Copy assignment — DELETED
    GpuBufferOwner& operator=(const GpuBufferOwner&) = delete;

    // ④ Move constructor — transfer ownership
    GpuBufferOwner(GpuBufferOwner&& other) noexcept
        : size_(other.size_), ptr_(other.ptr_) {
        other.ptr_  = nullptr;   // ← disarm source (prevents double-free)
        other.size_ = 0;
    }

    // ⑤ Move assignment — transfer ownership
    GpuBufferOwner& operator=(GpuBufferOwner&& other) noexcept {
        if (this != &other) {
            if (ptr_) cudaFree(ptr_);   // release current resource
            ptr_  = other.ptr_;
            size_ = other.size_;
            other.ptr_  = nullptr;      // disarm source
            other.size_ = 0;
        }
        return *this;
    }

    void*       get()  const noexcept { return ptr_; }
    std::size_t size() const noexcept { return size_; }

private:
    std::size_t size_;
    void*       ptr_;
};
```

### Rule of Zero — prefer it when possible

If a class **only contains members that are themselves RAII objects** (smart pointers, `std::string`, etc.), you can rely on the compiler-generated defaults and write **zero** of the five functions:

```cpp
// ✅ Rule of Zero — all members manage themselves
struct PipelineConfig {
    std::string            name;
    std::vector<SourceCfg> sources;
    std::optional<InferCfg> pgie;
    // compiler generates all five correctly — no manual definitions needed
};
```

### Quick-reference: when to write each

| Class holds... | Copy | Move | Destructor |
|---|---|---|---|
| Only value types / smart ptrs | `= default` | `= default` | `= default` |
| Sole-ownership raw resource | `= delete` | ✅ write | ✅ write |
| Shared raw resource | ✅ write (ref-count) | ✅ write | ✅ write |
| Non-transferable resource (mutex) | `= delete` | `= delete` | ✅ write |

---

## 13. GPU / CUDA Resources (DeepStream)

DeepStream pipelines allocate GPU memory, CUDA streams, and NvBufSurface objects. These must  
be freed with the correct CUDA/NvBuf APIs — **not** `delete` or `free`.

### CUDA device memory

```cpp
#include <cuda_runtime.h>

class CudaDeviceBuffer {
public:
    explicit CudaDeviceBuffer(std::size_t bytes) : bytes_(bytes) {
        if (cudaMalloc(&ptr_, bytes) != cudaSuccess)
            throw std::runtime_error("cudaMalloc failed");
    }
    ~CudaDeviceBuffer() noexcept { if (ptr_) cudaFree(ptr_); }

    CudaDeviceBuffer(const CudaDeviceBuffer&)            = delete;
    CudaDeviceBuffer& operator=(const CudaDeviceBuffer&) = delete;

    CudaDeviceBuffer(CudaDeviceBuffer&& o) noexcept
        : bytes_(o.bytes_), ptr_(o.ptr_) { o.ptr_ = nullptr; }

    void*       get()  const noexcept { return ptr_; }
    std::size_t size() const noexcept { return bytes_; }

private:
    std::size_t bytes_ = 0;
    void*       ptr_   = nullptr;
};
```

### CUDA stream

```cpp
#include <cuda_runtime.h>

class CudaStreamGuard {
public:
    CudaStreamGuard() {
        if (cudaStreamCreate(&stream_) != cudaSuccess)
            throw std::runtime_error("cudaStreamCreate failed");
    }
    ~CudaStreamGuard() noexcept {
        if (stream_) {
            cudaStreamSynchronize(stream_);  // drain before destroy
            cudaStreamDestroy(stream_);
        }
    }

    CudaStreamGuard(const CudaStreamGuard&)            = delete;
    CudaStreamGuard& operator=(const CudaStreamGuard&) = delete;

    CudaStreamGuard(CudaStreamGuard&& o) noexcept : stream_(o.stream_) {
        o.stream_ = nullptr;
    }

    cudaStream_t get() const noexcept { return stream_; }

private:
    cudaStream_t stream_ = nullptr;
};

// Usage
CudaStreamGuard stream;
cudaMemcpyAsync(dst, src, bytes, cudaMemcpyDeviceToHost, stream.get());
cudaStreamSynchronize(stream.get());
// cudaStreamDestroy called automatically
```

### NvBufSurface (DeepStream unified memory)

```cpp
#include <nvbufsurface.h>

class NvBufSurfaceGuard {
public:
    explicit NvBufSurfaceGuard(NvBufSurfaceCreateParams params)
        : surf_(nullptr) {
        if (NvBufSurfaceCreate(&surf_, 1, &params) != 0)
            throw std::runtime_error("NvBufSurfaceCreate failed");
    }
    ~NvBufSurfaceGuard() noexcept {
        if (surf_) NvBufSurfaceDestroy(surf_);
    }

    NvBufSurfaceGuard(const NvBufSurfaceGuard&)            = delete;
    NvBufSurfaceGuard& operator=(const NvBufSurfaceGuard&) = delete;

    NvBufSurface* get() const noexcept { return surf_; }

private:
    NvBufSurface* surf_;
};
```

### CUDA memory ownership — quick reference

| Resource | Allocate | Release | RAII wrapper |
|---|---|---|---|
| Device memory | `cudaMalloc` | `cudaFree` | `CudaDeviceBuffer` |
| Pinned host memory | `cudaMallocHost` | `cudaFreeHost` | Custom class |
| Unified memory | `cudaMallocManaged` | `cudaFree` | Custom class |
| CUDA stream | `cudaStreamCreate` | `cudaStreamDestroy` | `CudaStreamGuard` |
| NvBufSurface | `NvBufSurfaceCreate` | `NvBufSurfaceDestroy` | `NvBufSurfaceGuard` |
| NvDsBatchMeta | pipeline-managed | **DO NOT FREE** | Raw pointer only |

---

## 14. RAII in Containers

### Storing RAII objects in `std::vector`

RAII objects stored in containers **must be movable** (move constructor + move assignment). Containers call move operations on reallocation.

```cpp
// ✅ vector of unique_ptr — always safe (unique_ptr is movable)
std::vector<std::unique_ptr<PipelineConfig>> configs;
configs.push_back(std::make_unique<PipelineConfig>());
configs.emplace_back(std::make_unique<PipelineConfig>());

// ✅ vector of movable RAII objects
std::vector<GstPipelineOwner> pipelines;
pipelines.emplace_back("pipeline_0");  // GstPipelineOwner must have move ctor
pipelines.emplace_back("pipeline_1");

// ❌ WRONG — storing non-movable RAII object by value
// std::vector<std::mutex> mutexes;  // WON'T COMPILE — mutex is not movable
// ✅ CORRECT — store in unique_ptr instead
std::vector<std::unique_ptr<std::mutex>> mutexes;
mutexes.emplace_back(std::make_unique<std::mutex>());
```

### `std::map` / `std::unordered_map` with RAII values

```cpp
// Used in HandlerManager to keep event handler instances alive
std::unordered_map<std::string, std::unique_ptr<IEventHandler>> handlers_;

// Register
handlers_["crop"]   = std::make_unique<CropObjectHandler>(config, producer, storage);
handlers_["record"] = std::make_unique<SmartRecordHandler>(config, producer);

// Retrieve (non-owning view)
IEventHandler* get_handler(const std::string& id) {
    auto it = handlers_.find(id);
    return (it != handlers_.end()) ? it->second.get() : nullptr;
}

// All handlers freed automatically when handlers_ is destroyed
```

### `std::optional` — RAII with optional ownership

```cpp
// Used when a resource may or may not exist at runtime
class PipelineManager {
    std::optional<GstBusGuard> bus_guard_;

public:
    bool init(GstElement* pipeline) {
        // Construct in-place only when pipeline exists
        bus_guard_.emplace(pipeline);
        return true;
    }

    void teardown() {
        bus_guard_.reset();  // explicitly destroy guard (calls ~GstBusGuard)
    }
};
```

### Container ownership patterns summary

| Scenario | Pattern |
|---|---|
| Sole ownership + polymorphism | `vector<unique_ptr<Base>>` |
| Shared ownership between containers | `vector<shared_ptr<T>>` |
| Non-owning view into another container | `vector<T*>` or `span<T>` |
| Named/keyed resource map | `unordered_map<string, unique_ptr<T>>` |
| Optional resource | `optional<T>` (T must be movable) |

---

## 15. `[[nodiscard]]` — Enforce RAII Usage

Mark factory functions and handle-returning functions with `[[nodiscard]]` to prevent callers from discarding the RAII wrapper (which would immediately destroy the resource).

```cpp
// ✅ Without [[nodiscard]] — easy to misuse
engine::core::utils::GstElementPtr make_gst_element(const char* factory, const char* name);

// Caller accidentally discards the guard → immediate destruction → resource gone!
make_gst_element("nvinfer", "pgie");        // compiles, but guard is destroyed instantly!
auto e = make_gst_element("nvinfer", "pgie");  // correct usage

// ✅ With [[nodiscard]] — compiler warns on discard
[[nodiscard]] engine::core::utils::GstElementPtr
make_gst_element(const char* factory, const char* name);

make_gst_element("nvinfer", "pgie");  // ← warning: ignoring return value
auto e = make_gst_element("nvinfer", "pgie");  // ← correct
```

### Apply to RAII factory/builder functions

```cpp
namespace engine::core::utils {

// All make_* functions should be [[nodiscard]]
[[nodiscard]] GstElementPtr make_gst_element(const char* factory, const char* name) {
    return GstElementPtr(gst_element_factory_make(factory, name), gst_object_unref);
}

// RAII guard constructors use [[nodiscard]] on the class itself (C++17)
// Mark the type as nodiscard — any discarded temporary warns
struct [[nodiscard]] ScopeExitGuard {
    template<typename F>
    explicit ScopeExitGuard(F f) : f_(std::move(f)) {}
    ~ScopeExitGuard() { f_(); }
    // ...
private:
    std::function<void()> f_;
};

// Discard → warning
ScopeExitGuard([](){ gst_deinit(); });       // ← warning: temporary immediately destroyed
auto guard = ScopeExitGuard([](){ gst_deinit(); }); // ← correct
```

### `[[nodiscard]]` on error-returning functions

```cpp
// When return value indicates success/failure, mark nodiscard to force check
[[nodiscard]] bool configure_elements(GstElement* bin);
[[nodiscard]] bool link_elements(GstElement* src, GstElement* sink);

// Compiler warning if caller ignores result:
configure_elements(bin);              // ← warning: result ignored
if (!configure_elements(bin)) { ... } // ← correct
```

---

## 16. Exception Safety Guarantees

C++ RAII enables three levels of exception safety. Every function in vms-engine should aim for at least the **strong guarantee**.

### The Three Guarantees

| Level | Guarantee |
|---|---|
| **No-throw** (`noexcept`) | Function never throws. State unchanged. |
| **Strong** (commit-or-rollback) | If it throws, state is exactly as before the call. |
| **Basic** | If it throws, object is in a valid (but unspecified) state. No leaks. |

### Destructors must be `noexcept`

Destructors are implicitly `noexcept` in C++11+. **Never throw from a destructor.**  
If an exception propagates during stack unwinding and a destructor also throws → `std::terminate`.

```cpp
// ✅ CORRECT — noexcept destructor
~GstPipelineOwner() noexcept {
    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);  // errors logged, not thrown
        gst_object_unref(pipeline_);
    }
}

// ❌ WRONG — destructor that may throw
~BadResource() {
    if (resource_) {
        if (!release_resource(resource_))
            throw std::runtime_error("release failed");  // ← NEVER DO THIS
    }
}
```

### Strong guarantee with copy-and-swap

```cpp
class ConfigOwner {
public:
    explicit ConfigOwner(const std::string& path)
        : config_(load_config(path)) {}  // throws if file missing → object never constructed

    // Strong guarantee via copy-and-swap idiom
    ConfigOwner& operator=(ConfigOwner other) noexcept {  // copy made in param
        swap(*this, other);                                // noexcept swap
        return *this;
        // old value destroyed in 'other' destructor — clean rollback if copy threw
    }

    friend void swap(ConfigOwner& a, ConfigOwner& b) noexcept {
        using std::swap;
        swap(a.config_, b.config_);
    }

private:
    PipelineConfig config_;
};
```

### Using `ScopeGuard` for strong guarantee in multi-step init

```cpp
// Strong guarantee: if any step fails, all acquired resources are released
bool PipelineBuilder::build_phase(const PipelineConfig& cfg) {
    auto infer = engine::core::utils::make_gst_element("nvinfer", "pgie");
    if (!infer) return false;

    auto tracker = engine::core::utils::make_gst_element("nvtracker", "tracker");
    if (!tracker) return false;   // infer auto-released by ~GstElementPtr

    if (!gst_bin_add(GST_BIN(bin_), infer.get()))   return false;
    infer.release();  // bin owns

    if (!gst_bin_add(GST_BIN(bin_), tracker.get())) return false;
    tracker.release();

    return true;
    // Strong guarantee: any false return leaves bin in same state as before call
}
```

### Exception safety summary table

| Code pattern | Level achieved |
|---|---|
| Destructor-only cleanup | Basic |
| RAII + `noexcept` move + `noexcept` swap | Strong |
| `[[nodiscard]]` + RAII on all resources | Strong |
| `= delete` copy + `noexcept` move | Strong (no accidental copy mistakes) |
| Throwing from destructor | **Undefined Behaviour** — never do this |

---

*See also:*
- [`ARCHITECTURE_BLUEPRINT.md`](ARCHITECTURE_BLUEPRINT.md#memory-management) — project memory management summary
- [`CMAKE.md`](CMAKE.md) — build system documentation
- [`gst_utils.hpp`](../../core/include/engine/core/utils/gst_utils.hpp) — GStreamer RAII helpers to create
- [`docs/plans/phase1_refactor/02_core_layer.md`](../plans/phase1_refactor/02_core_layer.md) — task to create `gst_utils.hpp`
