#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>

namespace mali_wrapper {

class Config {
public:
    static Config& Instance();
    
    bool LoadFromFile(const std::string& path);
    void LoadFromEnvironment();
    
    bool IsExtensionEnabled(const std::string& name);
    std::string GetExtensionSetting(const std::string& ext, const std::string& key);
    std::string GetMaliDriverPath();
    
    std::string GetValue(const std::string& section, const std::string& key, const std::string& default_value = "");
    bool GetBoolValue(const std::string& section, const std::string& key, bool default_value = false);
    uint64_t GetUInt64Value(const std::string& section, const std::string& key, uint64_t default_value = 0);
    
private:
    Config() = default;
    ~Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> config_;
    
    void ParseLine(const std::string& line, std::string& current_section);
    std::string Trim(const std::string& str);
};

} // namespace mali_wrapper