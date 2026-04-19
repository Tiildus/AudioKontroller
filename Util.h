// =============================================================================
// Util.h — Cross-cutting utilities (XDG paths, process lookup, string helpers)
//
// All functions are inline header-only — no Util.cpp needed.
// =============================================================================

#pragma once
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>

// --- XDG Base Directory helpers ---
// Follow the XDG Base Directory spec for standard Linux file placement:
//   Config  → $XDG_CONFIG_HOME  (~/.config)
//   State   → $XDG_STATE_HOME   (~/.local/state)
//   Runtime → $XDG_RUNTIME_DIR  (/run/user/<uid>)

inline std::string xdgConfigDir() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg) return std::string(xdg) + "/audiokontroller";
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.config/audiokontroller";
}

inline std::string xdgStateDir() {
    const char* xdg = std::getenv("XDG_STATE_HOME");
    if (xdg) return std::string(xdg) + "/audiokontroller";
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.local/state/audiokontroller";
}

inline std::string xdgRuntimeDir() {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg) return std::string(xdg) + "/audiokontroller";
    return "/tmp/audiokontroller-" + std::to_string(getuid());
}

inline std::string resolveConfigPath() {
    return xdgConfigDir() + "/config.json";
}

inline std::string resolveLogPath() {
    return xdgStateDir() + "/audiokontroller.log";
}

// Reads /proc/<pid>/comm to get the process's binary name (e.g. "discord",
// "firefox"). Returns an empty string if the file can't be read (process
// exited or no permission). std::getline strips the trailing newline.
inline std::string getProcessName(int pid) {
    std::ifstream f("/proc/" + std::to_string(pid) + "/comm");
    std::string name;
    std::getline(f, name);
    return name;
}

// Spawns an external program via fork/exec. Fire-and-forget — the parent
// returns immediately without waiting. Avoids std::system() because:
//   - std::system() runs through /bin/sh and is vulnerable to shell injection
//     if argv contains user-controlled strings (e.g. config values).
//   - std::system() blocks until the child exits, which would stall the HID
//     read loop here.
// SA_NOCLDWAIT (set in main.cpp) reaps the child so no zombie accumulates.
// _exit() is used in the child (not exit()) to avoid flushing the parent's
// stdio buffers or running its destructors.
inline void forkExec(const std::vector<std::string>& argv) {
    pid_t pid = fork();
    if (pid == 0) {
        std::vector<const char*> args;
        for (const auto& a : argv) args.push_back(a.c_str());
        args.push_back(nullptr);
        execvp(args[0], const_cast<char* const*>(args.data()));
        _exit(127); // 127 is the conventional "command not found" exit code
    }
}

// Case-insensitive substring check: returns true if haystack contains needle
// (ignoring capitalization). For example, "Discord" contains "discord".
inline bool containsIgnoreCase(const std::string& haystack, const std::string& needle) {
    if (needle.empty() || needle.size() > haystack.size()) return false;
    auto it = std::search(haystack.begin(), haystack.end(),
                          needle.begin(), needle.end(),
                          [](char a, char b) { return std::tolower(static_cast<unsigned char>(a))
                                                   == std::tolower(static_cast<unsigned char>(b)); });
    return it != haystack.end();
}
