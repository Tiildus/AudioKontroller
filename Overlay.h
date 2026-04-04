// =============================================================================
// Overlay.h — KDE on-screen display (OSD) notifications
//
// Shows a brief overlay on screen when a knob is turned, similar to the
// volume popup that appears when you press a media key on the keyboard.
// Uses KDE Plasma's built-in OSD service via D-Bus — no extra dependencies.
// =============================================================================

#pragma once
#include <QString>

class Overlay {
public:
    Overlay() = default;

    // Displays the volume level (0.0–1.0) as a percentage in the KDE OSD.
    void showVolume(float volume);

private:
    // Sends a D-Bus message to KDE Plasma's OSD service.
    // This is the underlying implementation used by both public methods.
    void showKDEOSD(const QString &title, const QString &text);
};
