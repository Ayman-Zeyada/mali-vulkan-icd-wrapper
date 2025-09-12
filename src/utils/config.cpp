#include "config.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <cstdint>

namespace mali_wrapper {

Config& Config::Instance() {
    static Config instance;
    return instance;
}

bool Config::LoadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }
    
    std::string line;
    std::string current_section;
    
    while (std::getline(file, line)) {
        ParseLine(line, current_section);
    }
    
    return true;
}

void Config::LoadFromEnvironment() {
    if (const char* mali_path = std::getenv("MALI_DRIVER_PATH")) {
        config_["mali_driver"]["library_path"] = mali_path;
    }
    
    if (const char* log_level = std::getenv("MALI_WRAPPER_LOG_LEVEL")) {
        config_["logging"]["level"] = log_level;
    }
    
    if (const char* log_file = std::getenv("MALI_WRAPPER_LOG_FILE")) {
        config_["logging"]["output"] = log_file;
    }
}

bool Config::IsExtensionEnabled(const std::string& name) {
    return GetBoolValue("extensions", name, false);
}

std::string Config::GetExtensionSetting(const std::string& ext, const std::string& key) {
    return GetValue(ext, key);
}

std::string Config::GetMaliDriverPath() {
    return GetValue("mali_driver", "library_path", "/usr/lib/aarch64-linux-gnu/libmali.so");
}

std::string Config::GetValue(const std::string& section, const std::string& key, const std::string& default_value) {
    auto section_it = config_.find(section);
    if (section_it != config_.end()) {
        auto key_it = section_it->second.find(key);
        if (key_it != section_it->second.end()) {
            return key_it->second;
        }
    }
    return default_value;
}

bool Config::GetBoolValue(const std::string& section, const std::string& key, bool default_value) {
    std::string value = GetValue(section, key);
    if (value.empty()) {
        return default_value;
    }
    
    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
    return value == "true" || value == "1" || value == "yes" || value == "on";
}

uint64_t Config::GetUInt64Value(const std::string& section, const std::string& key, uint64_t default_value) {
    std::string value = GetValue(section, key);
    if (value.empty()) {
        return default_value;
    }
    
    try {
        if (value.substr(0, 2) == "0x" || value.substr(0, 2) == "0X") {
            return std::stoull(value, nullptr, 16);
        } else {
            return std::stoull(value);
        }
    } catch (const std::exception&) {
        return default_value;
    }
}

void Config::ParseLine(const std::string& line, std::string& current_section) {
    std::string trimmed = Trim(line);
    
    if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
        return;
    }
    
    if (trimmed[0] == '[' && trimmed.back() == ']') {
        current_section = trimmed.substr(1, trimmed.length() - 2);
        return;
    }
    
    size_t equals_pos = trimmed.find('=');
    if (equals_pos != std::string::npos) {
        std::string key = Trim(trimmed.substr(0, equals_pos));
        std::string value = Trim(trimmed.substr(equals_pos + 1));
        
        if (!current_section.empty() && !key.empty()) {
            config_[current_section][key] = value;
        }
    }
}

std::string Config::Trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
        return "";
    }
    
    size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

} // namespace mali_wrapper