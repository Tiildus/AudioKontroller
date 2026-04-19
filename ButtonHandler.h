// =============================================================================
// ButtonHandler.h — Executes actions triggered by PCPanel button presses
//
// Three actions are supported:
//   - "mediaPlayPause" : toggle Play/Pause via playerctl (MPRIS over D-Bus)
//   - "discordMute"    : toggle Discord's mic mute via Discord's local IPC
//                        socket. No synthetic keystroke, no ydotool — the
//                        mute is set inside Discord, so other call members
//                        see the muted icon update normally.
//   - "forceClose"     : SIGTERM the focused window's process (with a
//                        blocklist of protected system processes).
//
// External programs (currently just playerctl) are launched via the
// fork/exec helper in Util.h to avoid shell-injection risk and to keep
// the HID thread non-blocking.
//
// Two dependencies are injected so this class doesn't need to know about
// FocusMonitor or DiscordIPC directly:
//   - getPID()  : returns the focused window's PID (used by forceClose)
//   - DiscordIPC : the live IPC client (used by discordMute)
// =============================================================================

#pragma once
#include "ConfigManager.h"
#include <functional>
#include <string>

class DiscordIPC;

class ButtonHandler {
public:
    ButtonHandler() = default;

    // Injects a function that returns the focused window's PID.
    void setGetPIDFunc(std::function<int()> func) { getPID = func; }

    // Injects the Discord IPC client. May be nullptr; in that case the
    // "discordMute" action logs a warning and does nothing.
    void setDiscordIPC(DiscordIPC* ipc) { discord = ipc; }

    void handleButton(const ButtonConfig& bc);

    // Toggles media playback (uses playerctl).
    void toggleMediaPlayPause();

    // Toggles Discord's mic mute via the IPC client.
    void toggleDiscordMute();

    // Checks the focused process against a blocklist of protected system
    // processes (KDE, Wayland, audio stack, etc.). If safe, sends SIGTERM.
    void forceCloseFocusedWindow();

private:
    std::function<int()> getPID;
    DiscordIPC* discord = nullptr;
};
