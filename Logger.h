// =============================================================================
// Logger.h — Thread-safe file logger (singleton)
//
// Writes timestamped log lines to a file. Designed for a daemon that runs for
// hours — the file is truncated (not appended) on each start so it doesn't
// grow unboundedly.
//
// Usage:
//   Logger::instance().init("/path/to/file.log");
//   Logger::instance().info("Tag", "Something happened");
//   Logger::instance().warn("Tag", "Something unexpected");
//   Logger::instance().error("Tag", "Something failed");
//
// Thread safety: all methods are protected by a mutex, so any thread can log.
// Flush policy: INFO is buffered (low disk I/O for high-frequency events);
//               WARN and ERROR flush immediately so important messages are
//               never lost if the daemon crashes.
// =============================================================================

#pragma once
#include <string>
#include <fstream>
#include <mutex>

enum class LogLevel { INFO, WARN, ERROR };

class Logger {
public:
    // Returns the single global instance. Constructed on first call (thread-safe in C++11+).
    static Logger& instance();

    // Opens the log file and writes a session start header.
    // Must be called before any log() / info() / warn() / error() calls.
    void init(const std::string& filePath);

    void log(LogLevel level, const std::string& tag, const std::string& message);
    void info(const std::string& tag,  const std::string& message);
    void warn(const std::string& tag,  const std::string& message);
    void error(const std::string& tag, const std::string& message);
    void flush();

private:
    Logger() = default; // private: use instance() to access

    std::ofstream file;
    std::mutex mtx;       // protects file writes from concurrent threads
    bool initialized = false;

    std::string timestamp();
    std::string levelStr(LogLevel level);
};
