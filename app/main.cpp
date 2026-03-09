// app/main.cpp — VMS Engine entry point (DeepStream-only, engine:: namespace)
//
// Startup sequence:
//   1) parse_arguments → get config file path
//   2) YamlConfigParser::parse() → PipelineConfig
//   3) Set GST_ env vars; gst_init()
//   4) initialize_logger() from config
//   5) Log config summary
//   6) create_pipeline_manager() → PipelineBuilder + PipelineManager
//   7) pipeline_manager->initialize(config)
//   8) Register signal handlers
//   9) pipeline_manager->start()
//  10) Polling wait until pipeline stops
//  11) Unified cleanup + gst_deinit + spdlog::shutdown

// ─── Core headers ────────────────────────────────────────────────────────────
#include "engine/core/config/config_types.hpp"
#include "engine/core/config/iconfig_parser.hpp"
#include "engine/core/messaging/imessage_consumer.hpp"
#include "engine/core/messaging/imessage_producer.hpp"
#include "engine/core/pipeline/ipipeline_manager.hpp"
#include "engine/core/pipeline/pipeline_state.hpp"
#include "engine/core/utils/logger.hpp"
#include "engine/core/utils/spdlog_logger.hpp"

// ─── Infrastructure ───────────────────────────────────────────────────────────
#include "engine/infrastructure/config_parser/yaml_config_parser.hpp"
#include "engine/infrastructure/messaging/kafka_consumer.hpp"
#include "engine/infrastructure/messaging/redis_stream_producer.hpp"
#include "engine/infrastructure/messaging/redis_stream_consumer.hpp"
#include "engine/infrastructure/messaging/kafka_producer.hpp"

// ─── Pipeline ─────────────────────────────────────────────────────────────────
#include "engine/pipeline/pipeline_manager.hpp"
#include "engine/pipeline/pipeline_builder.hpp"

// ─── GStreamer / GLib ─────────────────────────────────────────────────────────
#include <glib.h>
#include <gst/gst.h>

// ─── Standard library ─────────────────────────────────────────────────────────
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <execinfo.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

// ─── Globals (async-signal-safe path needs only atomics) ──────────────────────
GMainLoop* g_main_loop = nullptr;  ///< sentinel — actual loop owned by PipelineManager
std::shared_ptr<engine::core::pipeline::IPipelineManager> g_pipeline_manager;
std::atomic<bool> g_stop_called{false};

// ─── Signal handlers ──────────────────────────────────────────────────────────

/**
 * @brief SIGINT / SIGTERM handler.
 *
 * Only the first delivery acts — subsequent signals are ignored.
 * Does not call stop() directly (not async-signal-safe); the main
 * polling loop calls stop() when it sees g_stop_called == true.
 */
void handle_signal(int signum) {
    bool expected = false;
    if (!g_stop_called.compare_exchange_strong(expected, true)) {
        return;  // already handled
    }

    // Write directly — async-signal-safe
    static const char msg[] = "\n[vms_engine] Signal received — stopping...\n";
    if (write(STDERR_FILENO, msg, sizeof(msg) - 1)) { /* best-effort */
    }
    (void)signum;
}

/**
 * @brief Crash signal handler (SIGSEGV, SIGABRT, SIGBUS, SIGFPE).
 *
 * Dumps a backtrace then re-raises the signal so the OS can generate a core
 * dump and the default disposition is preserved.
 */
void handle_crash_signal(int signum) {
    static const char header[] =
        "\n╔══════════════════════════════════════╗\n"
        "║   VMS Engine — FATAL CRASH SIGNAL    ║\n"
        "╚══════════════════════════════════════╝\n"
        "Backtrace:\n";

    if (write(STDERR_FILENO, header, sizeof(header) - 1)) { /* best-effort */
    }

    void* frames[32];
    int n = backtrace(frames, 32);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);

    // Re-raise with default disposition so the OS can generate a core dump
    signal(signum, SIG_DFL);
    raise(signum);
}

// ─── Argument parsing ─────────────────────────────────────────────────────────

/**
 * @brief Parse command-line arguments.
 * @return Path to the YAML config file (default: "configs/default.yml").
 */
std::string parse_arguments(int argc, char* argv[]) {
    std::string config_path = "configs/default.yml";

    for (int i = 1; i < argc; ++i) {
        // Skip GStreamer internal arguments
        if (std::strncmp(argv[i], "--gst-", 6) == 0) {
            continue;
        }

        if ((std::strcmp(argv[i], "-c") == 0 || std::strcmp(argv[i], "--config") == 0) &&
            (i + 1) < argc) {
            config_path = argv[++i];
        } else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            std::cout << "Usage: vms_engine [-c <config.yml>]\n"
                      << "  -c, --config <path>   YAML config file "
                         "(default: configs/default.yml)\n"
                      << "  -h, --help            Show this help\n";
            std::exit(EXIT_SUCCESS);
        }
    }

    return config_path;
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

/**
 * @brief Convert PipelineState enum to a human-readable string.
 */
const char* state_to_string(engine::core::pipeline::PipelineState s) {
    using engine::core::pipeline::PipelineState;
    switch (s) {
        case PipelineState::Uninitialized:
            return "Uninitialized";
        case PipelineState::Ready:
            return "Ready";
        case PipelineState::Playing:
            return "Playing";
        case PipelineState::Paused:
            return "Paused";
        case PipelineState::Stopped:
            return "Stopped";
        case PipelineState::Error:
            return "Error";
        default:
            return "Unknown";
    }
}

// ─── Messaging factory ────────────────────────────────────────────────────────

/**
 * @brief Create and connect a message producer from config.
 *
 * Returns nullptr if `config.messaging` is absent (publishing disabled).
 * Type selection: "redis" (default) or "kafka".
 *
 * @param config Full pipeline config; reads `config.messaging`.
 * @return Owning pointer to connected producer, or nullptr if disabled.
 */
std::unique_ptr<engine::core::messaging::IMessageProducer> create_message_producer(
    const engine::core::config::PipelineConfig& config) {
    if (!config.messaging) {
        LOG_D("No messaging config — producer disabled");
        return nullptr;
    }

    const auto& m = *config.messaging;

    if (m.type == "kafka") {
        LOG_I("Messaging: creating Kafka producer ({}:{})", m.host, m.port);
        auto p = std::make_unique<engine::infrastructure::messaging::KafkaProducer>();
        if (!p->connect(m.host, m.port)) {
            LOG_W("Messaging: Kafka connection failed ({}:{}) — continuing without producer",
                  m.host, m.port);
        }
        return p;
    }

    // Default: Redis
    LOG_I("Messaging: creating Redis producer ({}:{})", m.host, m.port);
    auto p = std::make_unique<engine::infrastructure::messaging::RedisStreamProducer>();
    if (!p->connect(m.host, m.port)) {
        LOG_W("Messaging: Redis connection failed ({}:{}) — continuing without producer", m.host,
              m.port);
    }
    return p;
}

/**
 * @brief Create and connect a message consumer for evidence_request intake.
 *
 * Returns nullptr if evidence or messaging is disabled.
 */
std::unique_ptr<engine::core::messaging::IMessageConsumer> create_message_consumer(
    const engine::core::config::PipelineConfig& config) {
    if (!config.messaging || !config.evidence || !config.evidence->enable) {
        return nullptr;
    }

    const auto& messaging = *config.messaging;
    const auto& evidence = *config.evidence;
    if (evidence.request_channel.empty()) {
        LOG_W("Messaging: evidence enabled but request_channel is empty");
        return nullptr;
    }

    if (messaging.type == "kafka") {
        LOG_I("Messaging: creating Kafka consumer ({}:{}) topic='{}' group='{}'", messaging.host,
              messaging.port, evidence.request_channel, config.pipeline.id);
        auto consumer = std::make_unique<engine::infrastructure::messaging::KafkaConsumer>();
        if (!consumer->connect(messaging.host, messaging.port, evidence.request_channel,
                               config.pipeline.id)) {
            LOG_W("Messaging: Kafka consumer connection failed ({}:{})", messaging.host,
                  messaging.port);
            return nullptr;
        }
        return consumer;
    }

    LOG_I("Messaging: creating Redis consumer ({}:{}) stream='{}' scope='{}'", messaging.host,
          messaging.port, evidence.request_channel, config.pipeline.id);
    auto consumer = std::make_unique<engine::infrastructure::messaging::RedisStreamConsumer>();
    if (!consumer->connect(messaging.host, messaging.port, evidence.request_channel,
                           config.pipeline.id)) {
        LOG_W("Messaging: Redis consumer connection failed ({}:{})", messaging.host,
              messaging.port);
        return nullptr;
    }
    return consumer;
}

// ─── Pipeline factory ─────────────────────────────────────────────────────────

/**
 * @brief Construct and return a fully wired PipelineManager.
 *
 * DeepStream-only: no #ifdef guards, no backend variants.
 */
std::shared_ptr<engine::core::pipeline::IPipelineManager> create_pipeline_manager() {
    auto builder = std::make_unique<engine::pipeline::PipelineBuilder>();
    return std::make_shared<engine::pipeline::PipelineManager>(std::move(builder));
}

}  // anonymous namespace

// ─── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    int return_code = EXIT_SUCCESS;

    // Declare pipeline_manager here so it is visible in catch/cleanup blocks
    std::shared_ptr<engine::core::pipeline::IPipelineManager> pipeline_manager;
    // Producer lifetime must cover the entire pipeline run
    std::unique_ptr<engine::core::messaging::IMessageProducer> message_producer;
    // Consumer lifetime must cover the entire pipeline run
    std::unique_ptr<engine::core::messaging::IMessageConsumer> message_consumer;

    try {
        // ── 1. Parse arguments ────────────────────────────────────────────────
        std::string config_path = parse_arguments(argc, argv);

        // ── 2. Parse YAML config ──────────────────────────────────────────────
        engine::infrastructure::config_parser::YamlConfigParser parser;
        engine::core::config::PipelineConfig config;

        if (!parser.parse(config_path, config)) {
            std::cerr << "[vms_engine] Failed to parse config: " << config_path << "\n";
            return EXIT_FAILURE;
        }

        // ── 3. Set GStreamer env vars; gst_init ───────────────────────────────
        if (!config.pipeline.dot_file_dir.empty()) {
            g_setenv("GST_DEBUG_DUMP_DOT_DIR", config.pipeline.dot_file_dir.c_str(), TRUE);
        }

        if (!config.pipeline.gst_log_level.empty()) {
            g_setenv("GST_DEBUG", config.pipeline.gst_log_level.c_str(), TRUE);
        }

        gst_init(&argc, &argv);

        // ── 4. Initialize logger ──────────────────────────────────────────────
        engine::core::utils::initialize_logger(config.pipeline.log_level, config.pipeline.log_file);

        // ── 5. Log config summary ─────────────────────────────────────────────
        LOG_I("══════════════════════════════════════════════════");
        LOG_I("  VMS Engine — starting");
        LOG_I("══════════════════════════════════════════════════");
        LOG_I("  Config     : {}", config_path);
        LOG_I("  Version    : {}", config.version.empty() ? "(unset)" : config.version);
        LOG_I("  Pipeline   : {} (id={})",
              config.pipeline.name.empty() ? "(unnamed)" : config.pipeline.name,
              config.pipeline.id.empty() ? "none" : config.pipeline.id);
        LOG_I("  Log level  : {}", config.pipeline.log_level);
        LOG_I("  GstDebug   : {}", config.pipeline.gst_log_level);
        LOG_I("  Cameras    : {}", config.sources.cameras.size());
        LOG_I("  BatchSize  : {}", config.sources.max_batch_size);
        LOG_I("  SmartRec   : {}", config.sources.smart_record ? "enabled" : "disabled");
        LOG_I("  Processing : {} element(s)", config.processing.elements.size());
        LOG_I("  Visuals    : {}", config.visuals.enable ? "enabled" : "disabled");
        LOG_I("  Outputs    : {}", config.outputs.size());
        LOG_I("  Handlers   : {}", config.event_handlers.size());
        if (config.messaging) {
            LOG_I("  Messaging  : {}://{}:{}", config.messaging->type, config.messaging->host,
                  config.messaging->port);
        } else {
            LOG_I("  Messaging  : disabled");
        }
        if (config.evidence) {
            LOG_I("  Evidence   : {} request='{}' ready='{}'",
                  config.evidence->enable ? "enabled" : "disabled",
                  config.evidence->request_channel, config.evidence->ready_channel);
        } else {
            LOG_I("  Evidence   : disabled");
        }
        LOG_I("══════════════════════════════════════════════════");

        // ── 6. Create pipeline manager ────────────────────────────────────────
        pipeline_manager = create_pipeline_manager();
        g_pipeline_manager = pipeline_manager;

        // ── 6a. Wire message producer (if configured) ─────────────────────────
        message_producer = create_message_producer(config);
        if (message_producer) {
            auto* concrete =
                dynamic_cast<engine::pipeline::PipelineManager*>(pipeline_manager.get());
            if (concrete) {
                concrete->set_message_producer(message_producer.get());
                LOG_I("Messaging: producer wired (type={})", config.messaging->type);
            } else {
                LOG_W("Messaging: could not wire producer — PipelineManager cast failed");
            }
        } else {
            LOG_I("Messaging: disabled (no 'messaging:' block in config)");
        }

        // ── 6b. Wire message consumer (if evidence configured) ───────────────
        message_consumer = create_message_consumer(config);
        if (message_consumer) {
            auto* concrete =
                dynamic_cast<engine::pipeline::PipelineManager*>(pipeline_manager.get());
            if (concrete) {
                concrete->set_message_consumer(message_consumer.get());
                LOG_I("Messaging: consumer wired for evidence_request");
            } else {
                LOG_W("Messaging: could not wire consumer — PipelineManager cast failed");
            }
        } else if (config.evidence && config.evidence->enable) {
            LOG_W("Messaging: evidence enabled but consumer is not available");
        }

        // ── 7. Initialize pipeline ────────────────────────────────────────────
        LOG_I("Initializing pipeline...");
        if (!pipeline_manager->initialize(config)) {
            LOG_C("Pipeline initialization failed — exiting");
            return EXIT_FAILURE;
        }
        LOG_I("Pipeline initialized (state=Ready)");

        // ── 8. Register signal handlers ───────────────────────────────────────
        signal(SIGINT, handle_signal);
        signal(SIGTERM, handle_signal);
        signal(SIGSEGV, handle_crash_signal);
        signal(SIGABRT, handle_crash_signal);
        signal(SIGBUS, handle_crash_signal);
        signal(SIGFPE, handle_crash_signal);

        // ── 9. Start pipeline ─────────────────────────────────────────────────
        LOG_I("Starting pipeline (PLAYING)...");
        if (!pipeline_manager->start()) {
            LOG_C("Pipeline start failed — exiting");
            return EXIT_FAILURE;
        }
        LOG_I("Pipeline playing — waiting for stop signal");

        // ── 10. Poll until pipeline reaches a terminal state ──────────────────
        using namespace std::chrono_literals;
        using engine::core::pipeline::PipelineState;

        while (true) {
            std::this_thread::sleep_for(100ms);

            PipelineState s = pipeline_manager->get_state();
            if (s == PipelineState::Stopped || s == PipelineState::Error) {
                LOG_I("Pipeline reached terminal state: {}", state_to_string(s));
                break;
            }
            if (g_stop_called.load()) {
                LOG_I("Stop requested via signal — stopping pipeline...");
                // Safe to call here (main thread, not signal handler)
                pipeline_manager->stop();
                break;
            }
        }

        // ── 11. Determine exit code; arm watchdog if error ────────────────────
        {
            PipelineState final_state = pipeline_manager->get_state();
            if (final_state == PipelineState::Error) {
                LOG_E("Pipeline exited with ERROR state");
                return_code = EXIT_FAILURE;

                // Arm watchdog: if cleanup hangs, force-exit after 10 seconds
                struct sigaction sa_wd {};
                sa_wd.sa_handler = [](int) {
                    static const char wdmsg[] =
                        "\n[vms_engine] Cleanup watchdog fired — force exiting\n";
                    if (write(STDERR_FILENO, wdmsg, sizeof(wdmsg) - 1)) {
                    }
                    _exit(1);
                };
                sigemptyset(&sa_wd.sa_mask);
                sa_wd.sa_flags = 0;
                sigaction(SIGALRM, &sa_wd, nullptr);
                alarm(10);
                LOG_W("Watchdog armed (10 s). Proceeding to cleanup...");
            } else {
                LOG_I("Pipeline exited normally (state={})", state_to_string(final_state));
                return_code = EXIT_SUCCESS;
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "\n[vms_engine] UNHANDLED EXCEPTION: " << e.what() << "\n";
        try {
            LOG_C("Unhandled exception in main: {}", e.what());
        } catch (...) {
        }
        return_code = EXIT_FAILURE;

    } catch (...) {
        std::cerr << "\n[vms_engine] UNKNOWN EXCEPTION in main\n";
        try {
            LOG_C("Unknown exception in main");
        } catch (...) {
        }
        return_code = EXIT_FAILURE;
    }

    // ── 12. Unified cleanup ───────────────────────────────────────────────────
    LOG_I("Starting cleanup sequence...");

    {
        // Use whichever pipeline_manager is valid
        auto mgr = pipeline_manager ? pipeline_manager : g_pipeline_manager;

        if (mgr && !g_stop_called.load()) {
            LOG_I("Stopping pipeline during cleanup...");
            mgr->stop();
            g_stop_called.store(true);
        } else if (g_stop_called.load()) {
            LOG_I("Pipeline already stopped");
        }

        // Clean up GMainLoop sentinel (PipelineManager owns the real one)
        if (g_main_loop) {
            if (!g_main_loop_is_running(g_main_loop)) {
                g_main_loop_unref(g_main_loop);
            }
            g_main_loop = nullptr;
        }

        LOG_I("Releasing pipeline manager...");
        pipeline_manager.reset();
        g_pipeline_manager.reset();

        // Brief yield for any in-flight GStreamer callbacks
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    LOG_I("All GStreamer objects released.");

    // Cancel watchdog — cleanup succeeded within the timeout window
    alarm(0);
    LOG_I("Watchdog cancelled. Cleanup completed successfully.");

    // ── Deinitialize GStreamer ─────────────────────────────────────────────────
    LOG_I("Deinitializing GStreamer...");

    {
        struct sigaction sa_gst {};
        sa_gst.sa_handler = [](int) {
            static const char m[] = "\n[vms_engine] gst_deinit() timeout — force exiting\n";
            if (write(STDERR_FILENO, m, sizeof(m) - 1)) {
            }
            _exit(EXIT_FAILURE);
        };
        sigemptyset(&sa_gst.sa_mask);
        sa_gst.sa_flags = 0;
        sigaction(SIGALRM, &sa_gst, nullptr);
        alarm(1);  // 1-second safety net

        try {
            gst_deinit();
            alarm(0);
            LOG_I("GStreamer deinitialized.");
        } catch (...) {
            alarm(0);
            LOG_W("Exception during gst_deinit() (ignored).");
        }
    }

    LOG_I("══════════════════════════════════════════════════");
    LOG_I("  VMS Engine — shutdown complete");
    LOG_I("══════════════════════════════════════════════════");

    spdlog::shutdown();
    return return_code;
}
