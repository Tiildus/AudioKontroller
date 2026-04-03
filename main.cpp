#include "PCPanelHandler.h"
#include "AudioHandler.h"
#include "Overlay.h"
#include "WindowUtils.h"
#include "ButtonHandler.h"
#include "ConfigManager.h"
#include "Logger.h"
#include <cstdlib>
#include <iostream>

// Expand a leading ~/ to the user's home directory
static std::string expandHome(const std::string& path) {
    if (path.size() >= 2 && path[0] == '~' && path[1] == '/') {
        const char* home = std::getenv("HOME");
        if (home) return std::string(home) + path.substr(1);
    }
    return path;
}

PCPanelDevice deviceFromString(const std::string& name) {
    if (name == "Pro") return PCPanelDevice::Pro;
    if (name == "RGB") return PCPanelDevice::RGB;
    return PCPanelDevice::Mini;
}

int main(int argc, char *argv[]) {
    // Load config (looks next to executable by default, or pass path as arg)
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

    Overlay overlay;
    AudioHandler audio;
    ButtonHandler button;
    PCPanelHandler panel(deviceFromString(cfg.device));
    panel.knobThreshold = cfg.knobThreshold;

    // --- Volume Controls (driven by config) ---
    panel.setCallback([&](int knob, float vol) {
        if (knob < 0 || knob >= static_cast<int>(cfg.knobs.size())) return;

        const KnobConfig& kc = cfg.knobs[knob];
        if (kc.type == "app") {
            audio.setVolumeForApp(kc.target, vol);
        } else if (kc.type == "focused") {
            audio.setVolumeForPID(getFocusedWindowPID(), vol);
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
        // "none" or unknown actions are silently ignored
    });

    panel.startListening();
}
