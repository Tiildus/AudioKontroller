#include "Overlay.h"
#include <QDBusMessage>
#include <QDBusConnection>

void Overlay::showVolume(float volume) {
    int percent = static_cast<int>(volume * 100.0f);
    showKDEOSD("Volume", QString::number(percent) + "%");
}

void Overlay::showText(const QString &title, const QString &text) {
    showKDEOSD(title, text);
}

void Overlay::showKDEOSD(const QString &title, const QString &text) {
    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.kde.plasmashell",
        "/org/kde/osdService",
        "org.kde.osdService",
        "showText"
    );
    msg << "audio-volume-high" << title + ": " + text;
    QDBusConnection::sessionBus().call(msg, QDBus::NoBlock);
}
