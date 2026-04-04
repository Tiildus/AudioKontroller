// =============================================================================
// ButtonHandler.cpp — Button action implementations
// =============================================================================

#include "ButtonHandler.h"
#include "Logger.h"
#include <unistd.h>   // fork, execvp, _exit
#include <signal.h>   // kill, SIGTERM, SIGKILL
#include <thread>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <cctype>

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

// Spawns an external program without going through a shell.
// Steps:
//   1. fork() duplicates this process. Both parent and child continue here.
//   2. In the child (pid == 0): build a null-terminated C argv array and call
//      execvp(), which replaces the child process image with the new program.
//   3. In the parent: fork() returns the child's PID. We ignore it and return.
//
// SA_NOCLDWAIT (set in main.cpp) prevents the child from becoming a zombie
// after it exits, since we never call wait() on it.
//
// We use _exit() instead of exit() in the child's error path because exit()
// flushes C++ destructors and stdio buffers from the parent process — running
// those in a forked child would corrupt shared file handles.
void ButtonHandler::forkExec(const std::vector<std::string>& argv) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process: build a null-terminated C-style argv array.
        // execvp() requires this format.
        std::vector<const char*> args;
        for (const auto& a : argv) args.push_back(a.c_str());
        args.push_back(nullptr);
        execvp(args[0], const_cast<char* const*>(args.data()));
        // If execvp returns, the exec failed (e.g. program not found).
        _exit(127); // 127 is the conventional "command not found" exit code
    }
    // Parent returns immediately. No wait() needed thanks to SA_NOCLDWAIT.
}

// Dispatches a button press to the appropriate action based on config.
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
    // Split on "+" and look up each key name
    std::vector<int> codes;
    std::istringstream stream(combo);
    std::string token;
    while (std::getline(stream, token, '+')) {
        // Trim whitespace and convert to lowercase for case-insensitive lookup
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

// Gracefully terminates the focused window's process:
//   1. SIGTERM — politely asks the process to exit (it can save state, etc.)
//   2. Wait 1 second.
//   3. If still running, SIGKILL — forcibly terminates it.
//
// This runs in a detached thread so the 1-second sleep doesn't stall the
// HID read loop (which would make knobs and other buttons feel unresponsive).
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

    std::thread([pid]() {
        kill(pid, SIGTERM);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        // kill(pid, 0) doesn't send a signal; it just checks if the process
        // still exists. Returns 0 if it does, -1 if it's gone.
        if (kill(pid, 0) == 0) {
            kill(pid, SIGKILL);
        }
        Logger::instance().info("ButtonHandler", "Force closed PID: " + std::to_string(pid));
    }).detach(); // detach so this thread cleans itself up when done
}
