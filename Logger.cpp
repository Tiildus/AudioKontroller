#include "Logger.h"
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::init(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(mtx);
    file.open(filePath, std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "Failed to open log file: " << filePath << "\n";
        return;
    }
    initialized = true;
    file << "\n=== AudioKontroller started at " << timestamp() << " ===\n";
    file.flush();
}

void Logger::log(LogLevel level, const std::string& tag, const std::string& message) {
    std::lock_guard<std::mutex> lock(mtx);
    if (initialized && file.is_open()) {
        file << timestamp() << " [" << levelStr(level) << "] [" << tag << "] " << message << "\n";
        if (level >= LogLevel::WARN) file.flush();
    }
}

void Logger::info(const std::string& tag, const std::string& message) {
    log(LogLevel::INFO, tag, message);
}

void Logger::warn(const std::string& tag, const std::string& message) {
    log(LogLevel::WARN, tag, message);
}

void Logger::error(const std::string& tag, const std::string& message) {
    log(LogLevel::ERROR, tag, message);
}

std::string Logger::timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm;
    localtime_r(&time, &tm);

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
