#pragma once

#include <string>
#include <fstream>
#include <memory>

namespace mali_wrapper {

enum class LogLevel {
    ERROR = 0,
    WARN = 1,
    INFO = 2,
    DEBUG = 3
};

class Logger {
public:
    static Logger& Instance();
    
    void SetLevel(LogLevel level);
    void SetOutputFile(const std::string& path);
    void EnableConsole(bool enable);
    
    void Log(LogLevel level, const std::string& message);
    void Error(const std::string& message);
    void Warn(const std::string& message);
    void Info(const std::string& message);
    void Debug(const std::string& message);
    
private:
    Logger() = default;
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    LogLevel level_ = LogLevel::INFO;
    std::unique_ptr<std::ofstream> file_stream_;
    bool console_enabled_ = true;
    
    const char* LevelToString(LogLevel level);
};

} // namespace mali_wrapper

#define LOG_ERROR(msg) mali_wrapper::Logger::Instance().Error(msg)
#define LOG_WARN(msg) mali_wrapper::Logger::Instance().Warn(msg)
#define LOG_INFO(msg) mali_wrapper::Logger::Instance().Info(msg)
#define LOG_DEBUG(msg) mali_wrapper::Logger::Instance().Debug(msg)