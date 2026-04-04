// =============================================================================
// ConfigManager.h — JSON config file loading and default generation
//
// config.json controls everything that would otherwise require recompiling:
//   - Which PCPanel device model to use (Mini / Pro / RGB)
//   - What each knob controls (named app or "focused" window)
//   - What each button does (media, key sequence, force close, none)
//   - Dead zone threshold and log file path
//
// If the config file doesn't exist on first run, a sensible default is
// generated automatically so the daemon works out of the box.
// =============================================================================

#pragma once
#include <string>
#include <vector>
#include <QJsonObject>

// Describes what one physical knob controls.
struct KnobConfig {
    std::string type;   // "app"     — target a specific application by name
                        // "focused" — target whichever window has keyboard focus
                        // "system"  — target the default output device (master volume)
    std::string target; // the app binary name (only used when type == "app")
};

// Describes what one physical button does when pressed.
struct ButtonConfig {
    std::string action;              // "mediaPlayPause" — toggle play/pause via playerctl
                                     // "sendKeys"       — send a key sequence via ydotool
                                     // "forceClose"     — kill the focused window
                                     // "none"           — do nothing
    std::vector<std::string> args;   // ydotool arguments (only used for "sendKeys")
};

// The complete parsed configuration.
struct Config {
    std::string device        = "Mini"; // PCPanel model name
    int knobThreshold         = 4;      // minimum raw movement (0–255) to fire a callback
    std::string logFile       = "audiokontroller.log";
    std::vector<KnobConfig>   knobs;
    std::vector<ButtonConfig> buttons;
};

class ConfigManager {
public:
    ConfigManager() = default;

    // Loads and parses the JSON file at the given path.
    // If the file doesn't exist, calls createDefault() to generate one first.
    // Returns false if the file cannot be read or parsed.
    bool load(const std::string& path);

    // Writes a default config.json to the given path.
    // Returns false if the file cannot be written.
    bool createDefault(const std::string& path);

    // Returns the parsed config. Only valid after a successful load().
    const Config& get() const { return config; }

private:
    Config config;
    KnobConfig   parseKnob(const QJsonObject& obj);
    ButtonConfig parseButton(const QJsonObject& obj);
};
