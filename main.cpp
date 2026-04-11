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
#include "ConfigManager.h"
#include "Logger.h"
#include <QCoreApplication>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <unistd.h>

// Expands a leading "~/" to the user's home directory.
// PulseAudio and other tools use this convention; the C runtime does not
// expand it automatically.
static std::string expandHome(const std::string& path) {
    if (path.size() >= 2 && path[0] == '~' && path[1] == '/') {
        const char* home = std::getenv("HOME");
        if (home) return std::string(home) + path.substr(1);
    }
    return path;
}

static PCPanelDevice deviceFromString(const std::string& name) {
    if (name == "Pro") return PCPanelDevice::Pro;
    if (name == "RGB") return PCPanelDevice::RGB;
    return PCPanelDevice::Mini;
}

// Resolves the directory the binary lives in via /proc/self/exe.
// Used by the config and log subcommands to find files in the install.sh layout.
static std::string resolveInstallDir() {
    std::unique_ptr<char, decltype(&free)> exePath(realpath("/proc/self/exe", nullptr), &free);
    if (exePath) {
        std::string exeStr(exePath.get());
        auto lastSlash = exeStr.rfind('/');
        if (lastSlash != std::string::npos) return exeStr.substr(0, lastSlash);
    }
    return ".";
}

// --- CLI subcommands ---
// When invoked with a recognized subcommand, the binary execs into the
// appropriate tool (systemctl, editor, less) and never returns.
// Unrecognized arguments fall through to daemon mode.
static constexpr const char* SERVICE_NAME = "audiokontroller.service";

// Config and log files live next to the binary (install.sh layout).
static std::string resolveConfigPath() {
    return resolveInstallDir() + "/config.json";
}

static std::string resolveLogPath() {
    return resolveInstallDir() + "/audiokontroller.log";
}

static void printUsage() {
    std::cerr << "Usage: AudioKontroller {start|stop|restart|status|config|log}\n"
              << "       AudioKontroller [config-path]   (run as daemon)\n";
}

static void handleSubcommand(int argc, char* argv[]) {
    if (argc < 2) return;

    std::string cmd = argv[1];

    if (cmd == "start" || cmd == "stop" || cmd == "restart") {
        execlp("systemctl", "systemctl", "--user", cmd.c_str(), SERVICE_NAME, nullptr);
        perror("execlp systemctl");
        _exit(1);
    }
    if (cmd == "status") {
        execlp("systemctl", "systemctl", "--user", "status", SERVICE_NAME, nullptr);
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

    // When ButtonHandler spawns child processes (playerctl, ydotool) via
    // fork/exec, we need to prevent them from becoming zombies.
    // SA_NOCLDWAIT tells the kernel to discard child exit status automatically
    // instead of keeping it until the parent calls wait().
    struct sigaction sa_chld{};
    sa_chld.sa_handler = SIG_IGN;
    sa_chld.sa_flags = SA_NOCLDWAIT;
    sigaction(SIGCHLD, &sa_chld, nullptr);

    // Accept an optional config path as the first argument,
    // falling back to "config.json" in the working directory.
    std::string configPath = "config.json";
    if (argc > 1) configPath = argv[1];

    ConfigManager configMgr;
    if (!configMgr.load(configPath)) {
        // Config failure is the one error that goes to stderr, because the
        // log file path itself comes from the config — it hasn't been
        // initialized yet.
        std::cerr << "Failed to load config, exiting.\n";
        return 1;
    }
    const Config& cfg = configMgr.get();

    Logger::instance().init(expandHome(cfg.logFile));
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

    // Inject FocusMonitor's PID lookup into ButtonHandler and AudioHandler so they
    // can access the focused window's PID without depending on FocusMonitor directly.
    button.setGetPIDFunc([&focusMonitor]() { return focusMonitor.getPID(); });
    audio.setGetPIDFunc([&focusMonitor]() { return focusMonitor.getPID(); });

    PCPanelHandler panel(deviceFromString(cfg.device));
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
                if (pid > 0) {
                    char p[64];
                    snprintf(p, sizeof(p), "/proc/%d/comm", pid);
                    FILE *f = fopen(p, "r");
                    if (f) {
                        char buf[256];
                        if (fgets(buf, sizeof(buf), f)) {
                            target = buf;
                            while (!target.empty() &&
                                   (target.back() == '\n' || target.back() == '\r'))
                                target.pop_back();
                        }
                        fclose(f);
                    }
                }
            }
            overlay.showVolume(vol, kc.type, target);
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
