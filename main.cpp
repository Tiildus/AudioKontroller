#include "PCPanelHandler.h"
#include "AudioHandler.h"
#include "Overlay.h"
#include "FocusMonitor.h"
#include "ButtonHandler.h"
#include "ConfigManager.h"
#include "Logger.h"
#include <QCoreApplication>
#include <csignal>
#include <cstdlib>
#include <iostream>

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

// Signal handling
static PCPanelHandler* globalPanel = nullptr;

static void signalHandler(int) {
    if (globalPanel) globalPanel->requestStop();
    QCoreApplication::quit();
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    // Prevent zombie child processes from fire-and-forget forkExec
    struct sigaction sa_chld{};
    sa_chld.sa_handler = SIG_IGN;
    sa_chld.sa_flags = SA_NOCLDWAIT;
    sigaction(SIGCHLD, &sa_chld, nullptr);

    // Load config
    std::string configPath = "config.json";
    if (argc > 1) configPath = argv[1];

    ConfigManager configMgr;
    if (!configMgr.load(configPath)) {
        std::cerr << "Failed to load config, exiting.\n";
        return 1;
    }
    const Config& cfg = configMgr.get();

    // Initialize logger
    Logger::instance().init(expandHome(cfg.logFile));
    Logger::instance().info("Main", "Config loaded from " + configPath);

    // Determine install directory (where the binary lives) for writing KWin script
    std::string installDir = ".";
    char* exePath = realpath("/proc/self/exe", nullptr);
    if (exePath) {
        std::string exeStr(exePath);
        auto lastSlash = exeStr.rfind('/');
        if (lastSlash != std::string::npos) installDir = exeStr.substr(0, lastSlash);
        free(exePath);
    }

    Overlay overlay;
    AudioHandler audio;
    ButtonHandler button;
    FocusMonitor focusMonitor(installDir);

    // Wire up ButtonHandler to use FocusMonitor for PID
    button.setGetPIDFunc([&focusMonitor]() { return focusMonitor.getPID(); });

    PCPanelHandler panel(deviceFromString(cfg.device));
    panel.knobThreshold = cfg.knobThreshold;

    // Install signal handlers for clean shutdown
    globalPanel = &panel;
    struct sigaction sa{};
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    // --- Volume Controls (driven by config) ---
    panel.setCallback([&](int knob, float vol) {
        if (knob < 0 || knob >= static_cast<int>(cfg.knobs.size())) return;

        const KnobConfig& kc = cfg.knobs[knob];
        if (kc.type == "app") {
            audio.setVolumeForApp(kc.target, vol);
        } else if (kc.type == "focused") {
            int pid = focusMonitor.getPID();
            if (pid > 0) audio.setVolumeForPID(static_cast<uint32_t>(pid), vol);
        }
        overlay.showVolume(vol);
    });

    // --- Button Controls (driven by config) ---
    panel.setButtonCallback([&](int btn) {
        if (btn < 0 || btn >= static_cast<int>(cfg.buttons.size())) return;

        const ButtonConfig& bc = cfg.buttons[btn];
        if (bc.action == "mediaPlayPause") {
            button.toggleMediaPlayPause();
        } else if (bc.action == "sendKeys") {
            button.sendKeySequence(bc.args);
        } else if (bc.action == "forceClose") {
            button.forceCloseFocusedWindow();
        }
    });

    // Start HID reading in background thread
    panel.startListeningAsync();

    // Run Qt event loop on main thread (processes D-Bus callbacks from KWin)
    int ret = app.exec();

    // Clean shutdown
    panel.stopListening();
    Logger::instance().info("Main", "Shutdown complete");
    return ret;
}
