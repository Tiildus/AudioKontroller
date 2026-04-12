// =============================================================================
// ButtonHandler.h — Executes actions triggered by PCPanel button presses
//
// All actions that need to run an external program use fork/exec rather than
// std::system(). This avoids:
//   - Shell injection: std::system() passes the command through /bin/sh, so
//     any shell metacharacters in config values could execute arbitrary code.
//   - Blocking: std::system() waits for the child to finish; fork/exec lets
//     the parent return immediately so the HID read loop keeps running.
//
// The "focused window" PID is injected via setGetPIDFunc() so ButtonHandler
// doesn't need to know about FocusMonitor directly (keeps dependencies clean).
// =============================================================================

#pragma once
#include "ConfigManager.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <functional>

class ButtonHandler {
public:
    ButtonHandler() = default;

    // Injects a function that returns the focused window's PID.
    void setGetPIDFunc(std::function<int()> func) { getPID = func; }

    void handleButton(const ButtonConfig& bc);

    // Toggles media playback (uses playerctl).
    void toggleMediaPlayPause();

    // Sends a key sequence using ydotool. "args" is passed directly as
    // ydotool arguments (e.g. {"key", "29:1", "41:1", "41:0", "29:0"}).
    void sendKeySequence(const std::vector<std::string>& args);

    // Translates a human-readable key combo like "ctrl+grave" into the
    // ydotool key code:state arguments and executes it.
    void sendKeyCombo(const std::string& combo);

    // Checks the focused process against a blocklist of protected system
    // processes (KDE, Wayland, audio stack, etc.). If safe, sends SIGTERM.
    void forceCloseFocusedWindow();

private:
    // Holds the injected PID-lookup function. May be empty if not set.
    std::function<int()> getPID;

    // Forks a child process and immediately exec's the given command.
    // The parent returns without waiting — fire-and-forget.
    // argv[0] is the program name; remaining elements are its arguments.
    static void forkExec(const std::vector<std::string>& argv);

    // Maps human-readable key names (lowercase) to Linux input event keycodes.
    // These codes come from linux/input-event-codes.h and are what ydotool expects.
    static const std::unordered_map<std::string, int> keyMap;
};
