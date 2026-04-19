// =============================================================================
// ConfigManager.h — JSON config file loading and default generation
//
// config.json controls everything that would otherwise require recompiling:
//   - Which PCPanel device model to use (Mini / Pro / RGB)
//   - What each knob controls (named app or "focused" window)
//   - What each button does (media, key sequence, force close, none)
//   - Dead zone threshold, volume gamma curve, and optional log file path override
//
// If the config file doesn't exist on first run, a sensible default is
// generated automatically so the daemon works out of the box.
// =============================================================================

#pragma once
#include <string>
#include <vector>
#include <QJsonObject>

// Forward declaration — full definition is in PCPanelHandler.h.
// Allows Config to store the parsed device type without pulling in HID headers.
enum class PCPanelDevice;

// Describes what one physical knob controls.
struct KnobConfig {
    std::string type;   // "app"     — target a specific application by name
                        // "focused" — target whichever window has keyboard focus
                        // "system"  — target the default output device (master volume)

    // App binary name(s) to control (only used when type == "app").
    // Supports a single string or an array in config.json:
    //   { "type": "app", "target": "firefox" }
    //   { "type": "app", "target": ["chrome", "firefox"] }
    std::vector<std::string> targets;
};

// Describes what one physical button does when pressed.
struct ButtonConfig {
    std::string action;              // "mediaPlayPause" — toggle play/pause via playerctl
                                     // "discordMute"    — toggle Discord mic mute via IPC
                                     // "forceClose"     — kill the focused window
                                     // "none"           — do nothing
};

// The complete parsed configuration.
struct Config {
    PCPanelDevice device;      // PCPanel hardware variant (Mini / Pro / RGB)
    int knobThreshold;         // minimum raw movement (0–255) to fire a callback (clamped 0–50)
    float volumeGamma;         // gamma curve for app/focused volume (not system); min 0.1, default 0.35
    std::string logFile;       // empty = XDG default (~/.local/state/audiokontroller/)

    // Discord IPC credentials. Required only if at least one button uses the
    // "discordMute" action. Obtained by registering a free application at
    // https://discord.com/developers — see WALKTHROUGH.md for details.
    std::string discordClientId;
    std::string discordClientSecret;

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
