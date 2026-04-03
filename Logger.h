#pragma once
#include <string>
#include <fstream>
#include <mutex>

enum class LogLevel { INFO, WARN, ERROR };

class Logger {
public:
    static Logger& instance();

    void init(const std::string& filePath);
    void log(LogLevel level, const std::string& tag, const std::string& message);
    void info(const std::string& tag, const std::string& message);
    void warn(const std::string& tag, const std::string& message);
    void error(const std::string& tag, const std::string& message);

private:
    Logger() = default;
    std::ofstream file;
    std::mutex mtx;
    bool initialized = false;

    std::string timestamp();
    std::string levelStr(LogLevel level);
};
