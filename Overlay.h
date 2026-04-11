// =============================================================================
// Overlay.h — KDE on-screen display (OSD) notifications
//
// Shows a brief overlay on screen when a knob is turned, similar to the
// volume popup that appears when you press a media key on the keyboard.
// Uses KDE Plasma's built-in OSD service via D-Bus — no extra dependencies.
// =============================================================================

#pragma once
#include <QString>
#include <string>

class Overlay {
public:
    Overlay() = default;

    // Displays the volume level (0.0–1.0) as a progress bar in the KDE OSD.
    // |type|  — knob type ("app", "focused", "system")
    // |target| — first app binary name (used as icon for "app" knobs)
    void showVolume(float volume, const std::string &type = {},
                    const std::string &target = {});

private:
    // Sends a D-Bus showProgress call to KDE Plasma's OSD service.
    void showKDEOSD(const QString &icon, int percent, const QString &text);
};
