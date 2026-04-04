// =============================================================================
// Logger.cpp — Singleton logger implementation
// =============================================================================

#include "Logger.h"
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

// Static local variables are initialized once and are thread-safe in C++11+.
// This is the "Meyers singleton" pattern.
Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

// Opens the log file in truncate mode (ios::trunc) so each daemon session
// starts with a fresh file. Appending would let the file grow without limit
// over multiple restarts during a long day.
void Logger::init(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(mtx);
    file.open(filePath, std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "Failed to open log file: " << filePath << "\n";
        return;
    }
    initialized = true;
    file << "\n=== AudioKontroller started at " << timestamp() << " ===\n";
    file.flush(); // flush the header immediately so it's visible right away
}

void Logger::log(LogLevel level, const std::string& tag, const std::string& message) {
    std::lock_guard<std::mutex> lock(mtx);
    if (initialized && file.is_open()) {
        file << timestamp() << " [" << levelStr(level) << "] [" << tag << "] " << message << "\n";
        // Flush on WARN and ERROR so important messages are never lost in the
        // OS write buffer if the daemon crashes or is killed.
        // INFO messages are left buffered to reduce disk I/O during normal
        // operation (knobs fire many callbacks per second).
        if (level >= LogLevel::WARN) file.flush();
    }
}

void Logger::info(const std::string& tag,  const std::string& message) { log(LogLevel::INFO, tag, message); }
void Logger::warn(const std::string& tag,  const std::string& message) { log(LogLevel::WARN, tag, message); }
void Logger::error(const std::string& tag, const std::string& message) { log(LogLevel::ERROR, tag, message); }

// Produces a timestamp string like "2025-04-04 14:23:01.042".
// Millisecond precision helps correlate log events when debugging timing issues.
std::string Logger::timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm;
    localtime_r(&time, &tm); // thread-safe version of localtime (POSIX)

    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
       << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::string Logger::levelStr(LogLevel level) {
    switch (level) {
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
    }
    return "UNKNOWN";
}
