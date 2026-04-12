// =============================================================================
// ConfigManager.cpp — JSON config loading and default generation
// =============================================================================

#include "ConfigManager.h"
#include "Logger.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <iostream>

bool ConfigManager::load(const std::string& path) {
    QFile file(QString::fromStdString(path));

    if (!file.open(QIODevice::ReadOnly)) {
        // Config doesn't exist yet (first run). Generate defaults so the
        // user has something to edit. This error goes to stderr rather than
        // the logger because the log file path is inside the config itself —
        // the logger hasn't been initialized yet.
        std::cerr << "Config file not found: " << path << "\n";
        std::cerr << "Creating default config...\n";
        if (!createDefault(path)) {
            std::cerr << "Failed to create default config.\n";
            return false;
        }
        if (!file.open(QIODevice::ReadOnly)) {
            std::cerr << "Failed to open newly created config: " << path << "\n";
            return false;
        }
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();

    if (doc.isNull()) {
        std::cerr << "Failed to parse config: " << err.errorString().toStdString() << "\n";
        return false;
    }

    QJsonObject root = doc.object();

    // Read top-level fields with sensible defaults if any key is missing.
    config.device       = root.value("device").toString("Mini").toStdString();
    config.knobThreshold = root.value("knobThreshold").toInt(4);
    config.logFile      = root.value("logFile").toString("").toStdString();

    QJsonArray knobsArr = root.value("knobs").toArray();
    config.knobs.clear();
    for (const auto& val : knobsArr) {
        config.knobs.push_back(parseKnob(val.toObject()));
    }

    QJsonArray buttonsArr = root.value("buttons").toArray();
    config.buttons.clear();
    for (const auto& val : buttonsArr) {
        config.buttons.push_back(parseButton(val.toObject()));
    }

    return true;
}

// Generates a default config as a starting point for new installs.
// Creates parent directories if they don't exist (e.g. ~/.config/audiokontroller/).
bool ConfigManager::createDefault(const std::string& path) {
    QDir().mkpath(QFileInfo(QString::fromStdString(path)).absolutePath());
    QJsonObject root;
    root["device"]         = "Mini";
    root["knobThreshold"]  = 4;

    QJsonArray knobs;
    auto makeKnob = [](const QString& type, const QString& target = "") {
        QJsonObject obj;
        obj["type"] = type;
        if (!target.isEmpty()) obj["target"] = target;
        return obj;
    };
    knobs.append(makeKnob("app", "firefox"));
    knobs.append(makeKnob("app", "discord"));
    knobs.append(makeKnob("focused"));
    knobs.append(makeKnob("system"));
    root["knobs"] = knobs;

    QJsonArray buttons;
    {
        QJsonObject b;
        b["action"] = "mediaPlayPause";
        buttons.append(b);
    }
    {
        // Example: Ctrl+` combo using human-readable key names
        QJsonObject b;
        b["action"] = "sendKeys";
        b["keys"] = "ctrl+grave";
        buttons.append(b);
    }
    {
        QJsonObject b;
        b["action"] = "none";
        buttons.append(b);
    }
    {
        QJsonObject b;
        b["action"] = "forceClose";
        buttons.append(b);
    }
    root["buttons"] = buttons;

    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::WriteOnly)) {
        std::cerr << "Failed to write default config to: " << path << "\n";
        return false;
    }
    QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented)); // human-readable indented JSON
    file.close();
    return true;
}

// Parses a single knob entry from its JSON object.
// "target" can be a single string or an array of strings.
// Missing fields fall back to safe defaults.
KnobConfig ConfigManager::parseKnob(const QJsonObject& obj) {
    KnobConfig k;
    k.type = obj.value("type").toString("app").toStdString();

    QJsonValue targetVal = obj.value("target");
    if (targetVal.isArray()) {
        // Array form: { "target": ["chrome", "firefox"] }
        for (const auto& item : targetVal.toArray()) {
            std::string s = item.toString().toStdString();
            if (!s.empty()) k.targets.push_back(s);
        }
    } else if (targetVal.isString()) {
        // Single string form: { "target": "firefox" }
        std::string s = targetVal.toString().toStdString();
        if (!s.empty()) k.targets.push_back(s);
    }
    return k;
}

// Parses a single button entry from its JSON object.
// "keys" is a human-readable combo string (e.g. "ctrl+grave").
// "args" is the raw ydotool arguments fallback (e.g. ["key", "29:1", ...]).
// If both are present, "keys" takes priority.
ButtonConfig ConfigManager::parseButton(const QJsonObject& obj) {
    ButtonConfig b;
    b.action = obj.value("action").toString("none").toStdString();
    b.keys = obj.value("keys").toString("").toStdString();
    QJsonArray argsArr = obj.value("args").toArray();
    for (const auto& a : argsArr) {
        b.args.push_back(a.toString().toStdString());
    }
    return b;
}
