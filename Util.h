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
