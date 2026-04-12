// =============================================================================
// Overlay.cpp — KDE Plasma OSD via D-Bus
// =============================================================================

#include "Overlay.h"
#include <QDBusMessage>
#include <QDBusConnection>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QTextStream>

// Search XDG application directories (including Flatpak export paths) for a
// .desktop file whose name matches |processName|, then return its Icon= value.
//
// This handles two cases:
//   - Native apps: "firefox.desktop" → Icon=firefox
//   - Flatpak apps: "com.discordapp.Discord.desktop" matched by suffix
//                   ("discord") → Icon=com.discordapp.Discord
//
// Results are cached after the first lookup so repeated knob turns do not hit
// the filesystem.  Falls back to |processName| itself if no desktop file is
// found — this works for native apps whose binary name is also their icon name.
static QString resolveAppIcon(const QString &processName) {
    static QHash<QString, QString> cache;
    auto it = cache.constFind(processName);
    if (it != cache.constEnd())
        return *it;

    const QString home = QString::fromLocal8Bit(qgetenv("HOME"));
    const QStringList dirs = {
        QStringLiteral("/usr/share/applications"),
        QStringLiteral("/usr/local/share/applications"),
        home + "/.local/share/applications",
        QStringLiteral("/var/lib/flatpak/exports/share/applications"),
        home + "/.local/share/flatpak/exports/share/applications",
    };

    for (const QString &dirPath : dirs) {
        QDir d(dirPath);
        if (!d.exists()) continue;

        const QStringList files = d.entryList({QStringLiteral("*.desktop")},
                                              QDir::Files);
        for (const QString &filename : files) {
            // Strip ".desktop" (8 chars) and check for:
            //   exact match — "firefox"              == "firefox"
            //   contains    — "com.discordapp.Discord" contains "discord"
            const QString base = filename.chopped(8);
            const bool match =
                base.compare(processName, Qt::CaseInsensitive) == 0 ||
                base.contains(processName, Qt::CaseInsensitive);
            if (!match) continue;

            QFile f(d.filePath(filename));
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;

            QTextStream in(&f);
            bool inEntry = false;
            while (!in.atEnd()) {
                const QString line = in.readLine().trimmed();
                if (line == QLatin1String("[Desktop Entry]")) {
                    inEntry = true;
                    continue;
                }
                if (line.startsWith('[')) {
                    if (inEntry) break; // left [Desktop Entry] section
                    continue;
                }
                if (!inEntry) continue;
                if (line.startsWith(QLatin1String("Icon="))) {
                    const QString icon = line.mid(5);
                    cache[processName] = icon;
                    return icon;
                }
            }
        }
    }

    // No desktop file found — fall back to the process name itself.
    cache[processName] = processName;
    return processName;
}

void Overlay::showVolume(float volume, const std::string &target) {
    int percent = static_cast<int>(volume * 100.0f + 0.5f);

    // If we have a process name, resolve it to a freedesktop icon name via the
    // desktop file lookup.  This covers both native apps (binary name == icon
    // name) and Flatpaks (binary "discord" → icon "com.discordapp.Discord").
    // With no target fall back to a volume-level icon.
    QString icon;
    if (!target.empty())
        icon = resolveAppIcon(QString::fromStdString(target));
    else if (percent == 0)  icon = QStringLiteral("audio-volume-muted");
    else if (percent < 34)  icon = QStringLiteral("audio-volume-low");
    else if (percent < 67)  icon = QStringLiteral("audio-volume-medium");
    else                    icon = QStringLiteral("audio-volume-high");

    showKDEOSD(icon, percent, QStringLiteral("Volume"));
}

// Sends a D-Bus mediaPlayerVolumeChanged call to KDE Plasma's OSD service.
// "org.kde.plasmashell" / "/org/kde/osdService" is the standard KDE interface
// for showing transient on-screen overlays (the same system used by KDE's own
// volume and brightness keys).
//
// Arguments to mediaPlayerVolumeChanged():
//   arg 1: current percent (0–100)
//   arg 2: player name label (displayed as title)
//   arg 3: icon name (freedesktop icon theme name)
//
// This is the only method on the interface that shows a progress bar with a
// custom icon. showProgress does not exist as a callable method — it is only
// a signal that Plasma emits internally.
//
// QDBus::NoBlock sends the call and returns immediately without waiting for a
// reply — this is a "fire and forget" since we don't need confirmation that
// the OSD was shown.
void Overlay::showKDEOSD(const QString &icon, int percent,
                         const QString &text) {
    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.kde.plasmashell",
        "/org/kde/osdService",
        "org.kde.osdService",
        "mediaPlayerVolumeChanged"
    );
    msg << percent << text << icon;
    QDBusConnection::sessionBus().call(msg, QDBus::NoBlock);
}
