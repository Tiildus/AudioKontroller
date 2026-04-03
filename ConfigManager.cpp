#include "ConfigManager.h"
#include "Logger.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <iostream>

bool ConfigManager::load(const std::string& path) {
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly)) {
        std::cerr << "Config file not found: " << path << "\n";
        std::cerr << "Creating default config...\n";
        createDefault(path);
        file.open(QIODevice::ReadOnly);
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    file.close();

    if (doc.isNull()) {
        std::cerr << "Failed to parse config: " << err.errorString().toStdString() << "\n";
        return false;
    }

    QJsonObject root = doc.object();

    config.device = root.value("device").toString("Mini").toStdString();
    config.knobThreshold = root.value("knobThreshold").toInt(4);
    config.logFile = root.value("logFile").toString("audiokontroller.log").toStdString();

    // Parse knobs
    QJsonArray knobsArr = root.value("knobs").toArray();
    config.knobs.clear();
    for (const auto& val : knobsArr) {
        config.knobs.push_back(parseKnob(val.toObject()));
    }

    // Parse buttons
    QJsonArray buttonsArr = root.value("buttons").toArray();
    config.buttons.clear();
    for (const auto& val : buttonsArr) {
        config.buttons.push_back(parseButton(val.toObject()));
    }

    return true;
}

void ConfigManager::createDefault(const std::string& path) {
    QJsonObject root;
    root["device"] = "Mini";
    root["knobThreshold"] = 4;
    root["logFile"] = "audiokontroller.log";

    QJsonArray knobs;
    auto makeKnob = [](const QString& type, const QString& target = "") {
        QJsonObject obj;
        obj["type"] = type;
        if (!target.isEmpty()) obj["target"] = target;
        return obj;
    };
    knobs.append(makeKnob("app", "spotify"));
    knobs.append(makeKnob("app", "firefox"));
    knobs.append(makeKnob("app", "discord"));
    knobs.append(makeKnob("focused"));
    root["knobs"] = knobs;

    QJsonArray buttons;
    auto makeButton = [](const QString& action, const QJsonArray& args = {}) {
        QJsonObject obj;
        obj["action"] = action;
        if (!args.isEmpty()) obj["args"] = args;
        return obj;
    };
    buttons.append(makeButton("mediaPlayPause"));
    buttons.append(makeButton("sendKeys", QJsonArray({"type", "Hello World"})));
    buttons.append(makeButton("forceClose"));
    buttons.append(makeButton("none"));
    root["buttons"] = buttons;

    QFile file(QString::fromStdString(path));
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(root);
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        std::cout << "Default config created at: " << path << "\n";
    }
}

KnobConfig ConfigManager::parseKnob(const QJsonObject& obj) {
    KnobConfig k;
    k.type = obj.value("type").toString("app").toStdString();
    k.target = obj.value("target").toString("").toStdString();
    return k;
}

ButtonConfig ConfigManager::parseButton(const QJsonObject& obj) {
    ButtonConfig b;
    b.action = obj.value("action").toString("none").toStdString();
    QJsonArray argsArr = obj.value("args").toArray();
    for (const auto& a : argsArr) {
        b.args.push_back(a.toString().toStdString());
    }
    return b;
}
