// =============================================================================
// main.cpp — Entry point for the AudioKontroller daemon
//
// Responsible for:
//   1. Starting a Qt event loop (required for D-Bus communication)
//   2. Loading config and initializing all subsystems
//   3. Wiring subsystems together via callbacks
//   4. Handling OS signals (SIGTERM/SIGINT) for clean shutdown
// =============================================================================

#include "PCPanelHandler.h"
#include "AudioHandler.h"
#include "Overlay.h"
#include "FocusMonitor.h"
#include "ButtonHandler.h"
#include "DiscordIPC.h"
#include "ConfigManager.h"
#include "Logger.h"
#include "Util.h"
#include <QCoreApplication>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <unistd.h>

// --- CLI subcommands ---
// When invoked with a recognized subcommand, the binary execs into the
// appropriate tool (systemctl, editor, less) and never returns.
// Unrecognized arguments fall through to daemon mode.
static constexpr const char* SERVICE_NAME = "audiokontroller.service";

static void printUsage() {
    std::cerr << "Usage: audiokontroller {start|stop|restart|status|config|log}\n"
              << "       audiokontroller [config-path]   (run as daemon)\n";
}

static void handleSubcommand(int argc, char* argv[]) {
    if (argc < 2) return;

    std::string cmd = argv[1];

    if (cmd == "start" || cmd == "stop" || cmd == "restart" || cmd == "status") {
        execlp("systemctl", "systemctl", "--user", cmd.c_str(), SERVICE_NAME, nullptr);
        perror("execlp systemctl");
        _exit(1);
    }
    if (cmd == "config") {
        std::string path = resolveConfigPath();
        const char* editor = std::getenv("EDITOR");
        if (!editor) editor = "nano";
        execlp(editor, editor, path.c_str(), nullptr);
        perror("execlp editor");
        _exit(1);
    }
    if (cmd == "log") {
        std::string path = resolveLogPath();
        execlp("less", "less", "+G", path.c_str(), nullptr);
        perror("execlp less");
        _exit(1);
    }
    if (cmd == "--help" || cmd == "-h") {
        printUsage();
        _exit(0);
    }
}

// --- Signal handling ---
// Signal handlers run asynchronously and cannot safely call most functions.
// We keep a plain global pointer so the handler can call requestStop()
// (which only writes an atomic bool) and ask Qt to exit its event loop.
static PCPanelHandler* globalPanel = nullptr;

static void signalHandler(int) {
    if (globalPanel) globalPanel->requestStop();
    QCoreApplication::quit(); // tells app.exec() to return
}

int main(int argc, char *argv[]) {
    handleSubcommand(argc, argv);

    // --- Daemon mode ---
    // QCoreApplication is required for Qt's D-Bus support. It sets up the
    // event loop that FocusMonitor uses to receive callbacks from KWin and
    // to fire its retry timer.
    QCoreApplication app(argc, argv);

    // When ButtonHandler spawns child processes (playerctl) via
    // fork/exec, we need to prevent them from becoming zombies.
    // SA_NOCLDWAIT tells the kernel to discard child exit status automatically
    // instead of keeping it until the parent calls wait().
    struct sigaction sa_chld{};
    sa_chld.sa_handler = SIG_IGN;
    sa_chld.sa_flags = SA_NOCLDWAIT;
    sigaction(SIGCHLD, &sa_chld, nullptr);

    // Accept an optional config path as the first argument,
    // falling back to the XDG config directory.
    std::string configPath = resolveConfigPath();
    if (argc > 1) configPath = argv[1];

    ConfigManager configMgr;
    if (!configMgr.load(configPath)) {
        // Config failure goes to stderr because the logger hasn't been
        // initialized yet (it depends on a successful config load).
        std::cerr << "Failed to load config, exiting.\n";
        return 1;
    }
    const Config& cfg = configMgr.get();

    std::string logPath = cfg.logFile.empty() ? resolveLogPath() : cfg.logFile;
    Logger::instance().init(logPath);
    Logger::instance().info("Main", "Config loaded from " + configPath);

    // Construct all subsystems. The order matters for destruction (LIFO):
    // panel must be stopped before audio is torn down.
    Overlay overlay;
    AudioHandler audio;
    if (!audio.init()) {
        Logger::instance().warn("Main", "PulseAudio unavailable, volume control disabled");
    }
    ButtonHandler button;
    FocusMonitor focusMonitor; // registers D-Bus service, loads KWin script

    // Discord IPC client — only meaningful if credentials are set in config.
    // The client connects lazily on first toggleMute() call, so constructing
    // it here is cheap and never blocks startup if Discord isn't running.
    DiscordIPC discord(cfg.discordClientId, cfg.discordClientSecret);

    // Inject FocusMonitor's PID lookup into ButtonHandler and AudioHandler so they
    // can access the focused window's PID without depending on FocusMonitor directly.
    auto getFocusedPID = [&focusMonitor]() { return focusMonitor.getPID(); };
    button.setGetPIDFunc(getFocusedPID);
    button.setDiscordIPC(&discord);
    audio.setGetPIDFunc(getFocusedPID);
    audio.volumeGamma = cfg.volumeGamma;

    PCPanelHandler panel(cfg.device);
    panel.knobThreshold = cfg.knobThreshold;

    // Register signal handlers after all objects are constructed so
    // globalPanel is valid before any signal could arrive.
    globalPanel = &panel;
    struct sigaction sa{};
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, nullptr); // sent by systemd on service stop
    sigaction(SIGINT,  &sa, nullptr); // sent by Ctrl+C in terminal

    Logger::instance().info("Main", std::string("PCPanel: ") + (panel.isConnected() ? "connected" : "not found"));
    Logger::instance().info("Main", std::string("PulseAudio: ") + (audio.isConnected() ? "connected" : "unavailable"));
    Logger::instance().info("Main", std::string("FocusMonitor: ") + (focusMonitor.isScriptLoaded() ? "active" : "pending/failed"));
    Logger::instance().info("Main", std::string("DiscordIPC: ") + (discord.isConfigured() ? "configured" : "not configured"));

    // Flush startup messages so they're visible immediately in the log.
    // Normal runtime INFO messages stay buffered for performance.
    Logger::instance().flush();

    // --- Knob callback (HID read thread) ---
    panel.setCallback([&](int knob, float vol) {
        if (knob < 0 || knob >= static_cast<int>(cfg.knobs.size())) return;

        audio.handleKnob(cfg.knobs[knob], vol);

        // System volume already triggers KDE's own OSD, so skip ours to
        // avoid showing two overlapping popups at the same time.
        if (cfg.knobs[knob].type != "system") {
            const auto &kc = cfg.knobs[knob];
            std::string target;
            if (!kc.targets.empty()) {
                target = kc.targets.front();
            } else if (kc.type == "focused") {
                int pid = focusMonitor.getPID();
                if (pid > 0)
                    target = getProcessName(pid);
            }
            overlay.showVolume(vol, target);
        }
    });

    // --- Button callback (HID read thread) ---
    panel.setButtonCallback([&](int btn) {
        if (btn >= 0 && btn < static_cast<int>(cfg.buttons.size()))
            button.handleButton(cfg.buttons[btn]);
    });

    // Start HID polling in a background thread so the main thread stays free
    // to run the Qt event loop below.
    panel.startListeningAsync();

    // Run Qt's event loop on the main thread. This is what allows FocusMonitor
    // to receive D-Bus callbacks from KWin and fire its retry QTimer.
    // This call blocks until QCoreApplication::quit() is called (from the
    // signal handler above).
    int ret = app.exec();

    // Graceful shutdown: join the HID thread before destructors run.
    panel.stopListening();
    Logger::instance().info("Main", "Shutdown complete");
    return ret;
}
