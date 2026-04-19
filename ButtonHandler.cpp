// =============================================================================
// ButtonHandler.cpp — Button action implementations
// =============================================================================

#include "ButtonHandler.h"
#include "DiscordIPC.h"
#include "Logger.h"
#include "Util.h"
#include <signal.h>   // kill, SIGTERM
#include <unordered_set>

// Processes that should never be force-closed on KDE Wayland / Fedora.
// Killing any of these could crash the desktop session, corrupt system state,
// or cause hardware/audio issues that require a reboot or logout to recover.
static const std::unordered_set<std::string> closeBlocklist = {
    // KDE Plasma shell & compositing
    "plasmashell",       // KDE desktop shell (panels, widgets) — kill = no taskbar/desktop
    "kwin_wayland",      // KDE Wayland compositor — kill = entire graphical session dies
    "kwin_x11",          // KDE X11 compositor (fallback sessions)
    "ksmserver",         // KDE session manager — kill = can't log out cleanly
    "kded6",             // KDE daemon framework — hosts many background services
    "kded5",
    "kdeinit5",          // KDE process launcher
    "kdeinit6",

    // Wayland / display infrastructure
    "kwayland_seat",     // Wayland seat management
    "xwaylandvideobridge", // screen sharing bridge

    // Core system / init
    "systemd",           // PID 1 — killing this halts the OS
    "dbus-daemon",       // IPC bus — killing kills nearly every desktop service
    "dbus-broker",       // alternative D-Bus implementation used on Fedora

    // Audio stack — killing these mutes all audio until restarted
    "pipewire",
    "pipewire-pulse",    // PipeWire PulseAudio compatibility layer
    "wireplumber",       // PipeWire session/policy manager
    "pulseaudio",        // legacy, but occasionally still running

    // Login / authentication
    "sddm",              // display manager — kill = login screen gone
    "polkit-kde-authentication-agent-1", // privilege auth dialogs
    "polkitd",

    // NetworkManager — killing drops all network connections
    "NetworkManager",
    "nm-applet",

    // Fedora-specific background services
    "abrtd",             // crash reporter daemon
    "packagekitd",       // package management — kill mid-operation = broken packages
    "dnf",               // package manager — kill mid-install = broken packages

    // AudioKontroller itself — prevent accidental self-termination
    "audiokontroller",
};

void ButtonHandler::handleButton(const ButtonConfig& bc) {
    if (bc.action == "mediaPlayPause") {
        toggleMediaPlayPause();
    } else if (bc.action == "discordMute") {
        toggleDiscordMute();
    } else if (bc.action == "forceClose") {
        forceCloseFocusedWindow();
    }
    // "none" action intentionally does nothing
}

void ButtonHandler::toggleMediaPlayPause() {
    forkExec({"playerctl", "play-pause"});
    Logger::instance().info("ButtonHandler", "Toggled Play/Pause");
}

void ButtonHandler::toggleDiscordMute() {
    if (!discord) {
        Logger::instance().warn("ButtonHandler",
            "discordMute action triggered but no Discord IPC client is configured");
        return;
    }
    if (!discord->toggleMute()) {
        Logger::instance().warn("ButtonHandler", "Discord mute toggle failed");
    }
}

// Terminates the focused window's process:
//   1. Checks /proc/<pid>/comm against closeBlocklist — aborts if matched.
//   2. SIGTERM — politely asks the process to exit (it can save state, etc.)
void ButtonHandler::forceCloseFocusedWindow() {
    if (!getPID) {
        Logger::instance().warn("ButtonHandler", "No PID source configured");
        return;
    }
    int pid = getPID();
    if (pid <= 0) {
        Logger::instance().warn("ButtonHandler", "No focused PID found");
        return;
    }

    std::string name = getProcessName(pid);
    if (closeBlocklist.count(name)) {
        Logger::instance().warn("ButtonHandler",
            "Blocked force-close of protected process: " + name +
            " (PID " + std::to_string(pid) + ")");
        return;
    }

    // Re-check that the PID still belongs to the same process (guards against
    // PID reuse between the blocklist check above and the kill below).
    if (getProcessName(pid) != name) {
        Logger::instance().warn("ButtonHandler",
            "PID " + std::to_string(pid) + " was reused, aborting kill");
        return;
    }

    kill(pid, SIGTERM);
    Logger::instance().info("ButtonHandler",
        "Sent SIGTERM to " + name + " (PID " + std::to_string(pid) + ")");
}
