// =============================================================================
// ConfigManager.cpp — JSON config loading and default generation
// =============================================================================

#include "ConfigManager.h"
#include "Logger.h"
#include <QFile>
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
    config.logFile      = root.value("logFile").toString("audiokontroller.log").toStdString();

    // Parse each knob entry from the "knobs" JSON array.
    QJsonArray knobsArr = root.value("knobs").toArray();
    config.knobs.clear();
    for (const auto& val : knobsArr) {
        config.knobs.push_back(parseKnob(val.toObject()));
    }

    // Parse each button entry from the "buttons" JSON array.
    QJsonArray buttonsArr = root.value("buttons").toArray();
    config.buttons.clear();
    for (const auto& val : buttonsArr) {
        config.buttons.push_back(parseButton(val.toObject()));
    }

    return true;
}

// Writes a config file with four knobs (Spotify, Firefox, Discord, focused)
// and four buttons (play/pause, type text, force close, none) as a starting point.
bool ConfigManager::createDefault(const std::string& path) {
    QJsonObject root;
    root["device"]         = "Mini";
    root["knobThreshold"]  = 4;
    root["logFile"]        = "audiokontroller.log";

    // Build the knobs array
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

    // Build the buttons array
    QJsonArray buttons;
    auto makeButton = [](const QString& action, const QJsonArray& args = {}) {
        QJsonObject obj;
        obj["action"] = action;
        if (!args.isEmpty()) obj["args"] = args;
        return obj;
    };
    buttons.append(makeButton("mediaPlayPause"));
    buttons.append(makeButton("sendKeys", QJsonArray({"key", "29:1", "41:1", "41:0", "29:0"})));
    buttons.append(makeButton("none"));
    buttons.append(makeButton("forceClose"));
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
// Missing fields fall back to safe defaults.
KnobConfig ConfigManager::parseKnob(const QJsonObject& obj) {
    KnobConfig k;
    k.type   = obj.value("type").toString("app").toStdString();
    k.target = obj.value("target").toString("").toStdString();
    return k;
}

// Parses a single button entry from its JSON object.
// "args" is an optional array of strings (used by "sendKeys").
ButtonConfig ConfigManager::parseButton(const QJsonObject& obj) {
    ButtonConfig b;
    b.action = obj.value("action").toString("none").toStdString();
    QJsonArray argsArr = obj.value("args").toArray();
    for (const auto& a : argsArr) {
        b.args.push_back(a.toString().toStdString());
    }
    return b;
}
