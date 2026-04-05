// =============================================================================
// Overlay.cpp — KDE Plasma OSD via D-Bus
// =============================================================================

#include "Overlay.h"
#include <QDBusMessage>
#include <QDBusConnection>

void Overlay::showVolume(float volume) {
    int percent = static_cast<int>(volume * 100.0f + 0.5f);
    showKDEOSD("Volume", QString::number(percent) + "%");
}

// Sends a D-Bus method call to KDE Plasma's OSD service.
// "org.kde.plasmashell" / "/org/kde/osdService" is the standard KDE interface
// for showing transient on-screen overlays (the same system used by KDE's own
// volume and brightness keys).
//
// Arguments to showText():
//   arg 1: icon name ("audio-volume-high" uses the system icon theme)
//   arg 2: the text to display
//
// QDBus::NoBlock sends the call and returns immediately without waiting for a
// reply — this is a "fire and forget" since we don't need confirmation that
// the OSD was shown.
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
