// =============================================================================
// ButtonHandler.cpp — Button action implementations
// =============================================================================

#include "ButtonHandler.h"
#include "Logger.h"
#include "Util.h"
#include <unistd.h>   // fork, execvp, _exit
#include <signal.h>   // kill, SIGTERM
#include <sstream>
#include <algorithm>
#include <cctype>
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

// Key name to Linux keycode mapping (from linux/input-event-codes.h).
// ydotool uses these numeric codes in the format "CODE:1" (press) / "CODE:0" (release).
// Names are all lowercase; lookup normalizes input to lowercase before searching.
const std::unordered_map<std::string, int> ButtonHandler::keyMap = {
    // Modifiers
    {"ctrl",        29}, {"leftctrl",    29}, {"rightctrl",   97},
    {"shift",       42}, {"leftshift",   42}, {"rightshift",  54},
    {"alt",         56}, {"leftalt",     56}, {"rightalt",   100},
    {"super",      125}, {"leftmeta",   125}, {"rightmeta",  126},

    // Function keys
    {"f1",  59}, {"f2",  60}, {"f3",  61}, {"f4",  62}, {"f5",  63}, {"f6",  64},
    {"f7",  65}, {"f8",  66}, {"f9",  67}, {"f10", 68}, {"f11", 87}, {"f12", 88},

    // Number row
    {"1",  2}, {"2",  3}, {"3",  4}, {"4",  5}, {"5",  6},
    {"6",  7}, {"7",  8}, {"8",  9}, {"9", 10}, {"0", 11},

    // Letters
    {"a", 30}, {"b", 48}, {"c", 46}, {"d", 32}, {"e", 18}, {"f", 33},
    {"g", 34}, {"h", 35}, {"i", 23}, {"j", 36}, {"k", 37}, {"l", 38},
    {"m", 50}, {"n", 49}, {"o", 24}, {"p", 25}, {"q", 16}, {"r", 19},
    {"s", 31}, {"t", 20}, {"u", 22}, {"v", 47}, {"w", 17}, {"x", 45},
    {"y", 21}, {"z", 44},

    // Special keys
    {"esc",        1}, {"escape",      1},
    {"tab",       15},
    {"capslock",  58},
    {"enter",     28}, {"return",     28},
    {"backspace", 14},
    {"space",     57},
    {"delete",   111}, {"del",       111},
    {"insert",   110},
    {"home",     102}, {"end",       107},
    {"pageup",   104}, {"pagedown",  109},
    {"up",       103}, {"down",      108}, {"left", 105}, {"right", 106},
    {"printscreen", 99}, {"prtsc", 99},
    {"scrolllock",  70},
    {"pause",       119},

    // Punctuation / symbols (by name, since the actual character varies by layout)
    {"minus",        12}, {"-",          12},
    {"equal",        13}, {"=",          13},
    {"leftbrace",    26}, {"[",          26},
    {"rightbrace",   27}, {"]",          27},
    {"semicolon",    39}, {";",          39},
    {"apostrophe",   40}, {"'",          40},
    {"grave",        41}, {"`",          41},
    {"backslash",    43}, {"\\",         43},
    {"comma",        51}, {",",          51},
    {"dot",          52}, {".",          52},
    {"slash",        53}, {"/",          53},

    // Media keys
    {"mute",       113},
    {"volumedown", 114},
    {"volumeup",   115},
    {"playpause",  164},
    {"nextsong",   163}, {"next",    163},
    {"prevsong",   165}, {"prev",    165},
    {"stop",       166},
};

// Spawns an external program via fork/exec (no shell — immune to injection).
// SA_NOCLDWAIT (set in main.cpp) prevents zombies since we never call wait().
// _exit() is used instead of exit() in the child to avoid flushing the
// parent's stdio buffers or running its destructors.
void ButtonHandler::forkExec(const std::vector<std::string>& argv) {
    pid_t pid = fork();
    if (pid == 0) {
        std::vector<const char*> args;
        for (const auto& a : argv) args.push_back(a.c_str());
        args.push_back(nullptr);
        execvp(args[0], const_cast<char* const*>(args.data()));
        // If execvp returns, the exec failed (e.g. program not found).
        _exit(127); // 127 is the conventional "command not found" exit code
    }
    // Parent returns immediately. No wait() needed thanks to SA_NOCLDWAIT.
}

void ButtonHandler::handleButton(const ButtonConfig& bc) {
    if (bc.action == "mediaPlayPause") {
        toggleMediaPlayPause();
    } else if (bc.action == "sendKeys") {
        // "keys" field uses friendly names: "ctrl+grave"
        // "args" field uses raw ydotool arguments: ["key", "29:1", ...]
        if (!bc.keys.empty()) {
            sendKeyCombo(bc.keys);
        } else {
            sendKeySequence(bc.args);
        }
    } else if (bc.action == "forceClose") {
        forceCloseFocusedWindow();
    }
    // "none" action intentionally does nothing
}

void ButtonHandler::toggleMediaPlayPause() {
    forkExec({"playerctl", "play-pause"});
    Logger::instance().info("ButtonHandler", "Toggled Play/Pause");
}

// Builds the full ydotool command from the args in config.json and runs it.
// Because we use execvp (not a shell), the args are passed literally — no
// risk of shell injection from config values.
void ButtonHandler::sendKeySequence(const std::vector<std::string>& args) {
    std::vector<std::string> argv = {"ydotool"};
    argv.insert(argv.end(), args.begin(), args.end());
    forkExec(argv);

    std::string desc = "ydotool";
    for (const auto& a : args) desc += " " + a;
    Logger::instance().info("ButtonHandler", "Dispatched: " + desc);
}

// Translates a human-readable key combo string like "ctrl+shift+a" into the
// ydotool numeric format and executes it.
//
// The combo is split on "+" into individual key names, each looked up in keyMap.
// ydotool expects keys pressed in order then released in reverse order:
//   "ctrl+grave" -> ydotool key 29:1 41:1 41:0 29:0
//   (press ctrl, press grave, release grave, release ctrl)
void ButtonHandler::sendKeyCombo(const std::string& combo) {
    std::vector<int> codes;
    std::istringstream stream(combo);
    std::string token;
    while (std::getline(stream, token, '+')) {
        auto start = token.find_first_not_of(' ');
        auto end = token.find_last_not_of(' ');
        if (start == std::string::npos) continue;
        token = token.substr(start, end - start + 1);
        std::transform(token.begin(), token.end(), token.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        auto it = keyMap.find(token);
        if (it == keyMap.end()) {
            Logger::instance().warn("ButtonHandler", "Unknown key name: \"" + token + "\" in combo: " + combo);
            return;
        }
        codes.push_back(it->second);
    }

    if (codes.empty()) return;

    // Build ydotool argv: press all keys in order, then release in reverse
    std::vector<std::string> argv = {"ydotool", "key"};
    for (int code : codes)
        argv.push_back(std::to_string(code) + ":1");
    for (auto it = codes.rbegin(); it != codes.rend(); ++it)
        argv.push_back(std::to_string(*it) + ":0");

    forkExec(argv);
    Logger::instance().info("ButtonHandler", "Dispatched combo: " + combo);
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
