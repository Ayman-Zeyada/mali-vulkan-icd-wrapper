#include "logging.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstdlib>
#include <cstring>

namespace mali_wrapper {

Logger::Logger() {
    InitFromEnv();
}

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::SetLevel(LogLevel level) {
    level_ = level;
}

void Logger::SetCategory(LogCategory category) {
    category_ = category;
}

void Logger::EnableColors(bool enable) {
    colors_enabled_ = enable;
}

void Logger::SetOutputFile(const std::string& path) {
    if (!path.empty()) {
        file_stream_ = std::make_unique<std::ofstream>(path, std::ios::app);
    }
}

void Logger::EnableConsole(bool enable) {
    console_enabled_ = enable;
}

void Logger::Log(LogLevel level, LogCategory category, const std::string& message) {
    if (!ShouldLog(level, category)) {
        return;
    }

    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();

    if (console_enabled_ && colors_enabled_) {
        ss << " [" << GetColorCode(level) << LevelToString(level) << GetResetCode() << "]["
           << GetCategoryColor(category) << CategoryToString(category) << GetResetCode() << "] " << message;
    } else {
        ss << " [" << LevelToString(level) << "][" << CategoryToString(category) << "] " << message;
    }

    std::string log_line = ss.str();

    if (console_enabled_) {
        std::cout << log_line << std::endl;
    }

    if (file_stream_ && file_stream_->is_open()) {
        *file_stream_ << log_line << std::endl;
        file_stream_->flush();
    }
}

void Logger::LogF(LogLevel level, LogCategory category, const char* format, ...) {
    if (!ShouldLog(level, category)) {
        return;
    }

    va_list args;
    va_start(args, format);

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);

    va_end(args);

    Log(level, category, std::string(buffer));
}

void Logger::Error(const std::string& message) {
    Log(LogLevel::ERROR, LogCategory::WRAPPER, message);
}

void Logger::Warn(const std::string& message) {
    Log(LogLevel::WARN, LogCategory::WRAPPER, message);
}

void Logger::Info(const std::string& message) {
    Log(LogLevel::INFO, LogCategory::WRAPPER, message);
}

void Logger::Debug(const std::string& message) {
    Log(LogLevel::DEBUG, LogCategory::WRAPPER, message);
}

void Logger::WsiError(const std::string& message) {
    Log(LogLevel::ERROR, LogCategory::WSI_LAYER, message);
}

void Logger::WsiWarn(const std::string& message) {
    Log(LogLevel::WARN, LogCategory::WSI_LAYER, message);
}

void Logger::WsiInfo(const std::string& message) {
    Log(LogLevel::INFO, LogCategory::WSI_LAYER, message);
}

void Logger::WsiDebug(const std::string& message) {
    Log(LogLevel::DEBUG, LogCategory::WSI_LAYER, message);
}

void Logger::WsiLogF(LogLevel level, const char* format, ...) {
    if (!ShouldLog(level, LogCategory::WSI_LAYER)) {
        return;
    }

    va_list args;
    va_start(args, format);

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);

    va_end(args);

    Log(level, LogCategory::WSI_LAYER, std::string(buffer));
}

void Logger::InitFromEnv() {
    const char* log_level = std::getenv("MALI_WRAPPER_LOG_LEVEL");
    if (log_level) {
        int level = std::atoi(log_level);
        if (level >= 0 && level <= 3) {
            level_ = static_cast<LogLevel>(level);
        }
    }

    const char* log_category = std::getenv("MALI_WRAPPER_LOG_CATEGORY");
    if (log_category) {
        LogCategory parsed_category = ParseCategory(log_category);
        if (parsed_category == LogCategory::NONE) {
            LogCategoryWarning(log_category);
            category_ = LogCategory::NONE; // Disable logging for invalid category
        } else {
            category_ = parsed_category;
        }
    }

    const char* console = std::getenv("MALI_WRAPPER_LOG_CONSOLE");
    if (console && std::strcmp(console, "0") == 0) {
        console_enabled_ = false;
    }

    const char* colors = std::getenv("MALI_WRAPPER_LOG_COLORS");
    if (colors && std::strcmp(colors, "0") == 0) {
        colors_enabled_ = false;
    }

    const char* log_file = std::getenv("MALI_WRAPPER_LOG_FILE");
    if (log_file) {
        SetOutputFile(log_file);
    }
}

bool Logger::ShouldLog(LogLevel level, LogCategory category) const {
    if (level > level_ || category_ == LogCategory::NONE) {
        return false;
    }

    switch (category_) {
        case LogCategory::WRAPPER:
            return category == LogCategory::WRAPPER;
        case LogCategory::WSI_LAYER:
            return category == LogCategory::WSI_LAYER;
        case LogCategory::WRAPPER_WSI:
            return category == LogCategory::WRAPPER || category == LogCategory::WSI_LAYER;
        case LogCategory::NONE:
            return false;
        default:
            return true;
    }
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

LogCategory Logger::ParseCategory(const char* category_str) {
    if (!category_str) return LogCategory::WRAPPER_WSI;

    if (std::strcmp(category_str, "wrapper") == 0) {
        return LogCategory::WRAPPER;
    } else if (std::strcmp(category_str, "wsi") == 0) {
        return LogCategory::WSI_LAYER;
    } else if (std::strcmp(category_str, "wrapper+wsi") == 0 || std::strcmp(category_str, "wsi+wrapper") == 0) {
        return LogCategory::WRAPPER_WSI;
    } else {
        return LogCategory::NONE; // Invalid category
    }
}

std::string Logger::GetColorCode(LogLevel level) const {
    if (!colors_enabled_) return "";

    switch (level) {
        case LogLevel::ERROR: return "\033[1;31m"; // Bold Red
        case LogLevel::WARN:  return "\033[1;33m"; // Bold Yellow
        case LogLevel::INFO:  return "\033[1;36m"; // Bold Cyan
        case LogLevel::DEBUG: return "\033[1;35m"; // Bold Magenta
        default: return "";
    }
}

std::string Logger::GetResetCode() const {
    return colors_enabled_ ? "\033[0m" : "";
}

std::string Logger::GetCategoryColor(LogCategory category) const {
    if (!colors_enabled_) return "";

    switch (category) {
        case LogCategory::WRAPPER: return "\033[1;32m"; // Bold Green
        case LogCategory::WSI_LAYER: return "\033[1;34m"; // Bold Blue
        case LogCategory::WRAPPER_WSI: return "\033[1;37m"; // Bold White
        case LogCategory::NONE: return "\033[1;31m"; // Bold Red
        default: return "";
    }
}

const char* Logger::CategoryToString(LogCategory category) {
    switch (category) {
        case LogCategory::WRAPPER: return "WRAPPER";
        case LogCategory::WSI_LAYER: return "WSI";
        case LogCategory::WRAPPER_WSI: return "WRAPPER+WSI";
        case LogCategory::NONE: return "NONE";
        default: return "UNKNOWN";
    }
}

void Logger::LogCategoryWarning(const char* invalid_category) {
    fprintf(stderr, "\033[1;31m[WARNING]\033[0m Unknown log category '%s'. Valid options: wrapper, wsi, wrapper+wsi, wsi+wrapper. Logging disabled.\n", invalid_category);
}

} // namespace mali_wrapper