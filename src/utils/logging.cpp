#include "logging.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace mali_wrapper {

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::SetLevel(LogLevel level) {
    level_ = level;
}

void Logger::SetOutputFile(const std::string& path) {
    if (!path.empty()) {
        file_stream_ = std::make_unique<std::ofstream>(path, std::ios::app);
    }
}

void Logger::EnableConsole(bool enable) {
    console_enabled_ = enable;
}

void Logger::Log(LogLevel level, const std::string& message) {
    if (level > level_) {
        return;
    }
    
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    ss << " [" << LevelToString(level) << "] " << message;
    
    std::string log_line = ss.str();
    
    if (console_enabled_) {
        std::cout << log_line << std::endl;
    }
    
    if (file_stream_ && file_stream_->is_open()) {
        *file_stream_ << log_line << std::endl;
        file_stream_->flush();
    }
}

void Logger::Error(const std::string& message) {
    Log(LogLevel::ERROR, message);
}

void Logger::Warn(const std::string& message) {
    Log(LogLevel::WARN, message);
}

void Logger::Info(const std::string& message) {
    Log(LogLevel::INFO, message);
}

void Logger::Debug(const std::string& message) {
    Log(LogLevel::DEBUG, message);
}

const char* Logger::LevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::DEBUG: return "DEBUG";
        default: return "UNKNOWN";
    }
}

} // namespace mali_wrapper