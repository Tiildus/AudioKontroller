#pragma once
#include <string>
#include <vector>
#include <QJsonObject>

struct KnobConfig {
    std::string type;    // "app" or "focused"
    std::string target;  // app name (only for type "app")
};

struct ButtonConfig {
    std::string action;                // "mediaPlayPause", "sendKeys", "forceClose", "none"
    std::vector<std::string> args;     // arguments for sendKeys action
};

struct Config {
    std::string device;                // "Mini", "Pro", "RGB"
    int knobThreshold = 4;
    std::string logFile = "audiokontroller.log";
    std::vector<KnobConfig> knobs;
    std::vector<ButtonConfig> buttons;
};

class ConfigManager {
public:
    ConfigManager() = default;

    bool load(const std::string& path);
    bool createDefault(const std::string& path);
    const Config& get() const { return config; }

private:
    Config config;
    KnobConfig parseKnob(const QJsonObject& obj);
    ButtonConfig parseButton(const QJsonObject& obj);
};
