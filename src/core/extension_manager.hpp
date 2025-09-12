#pragma once

#include "../extensions/base_extension.hpp"
#include <memory>
#include <vector>
#include <unordered_map>
#include <string>

namespace mali_wrapper {

class ExtensionManager {
public:
    static ExtensionManager& Instance();
    
    void RegisterExtension(std::unique_ptr<BaseExtension> extension);
    BaseExtension* GetExtension(const std::string& name);
    
    std::vector<VkExtensionProperties> GetSupportedInstanceExtensions();
    std::vector<VkExtensionProperties> GetSupportedDeviceExtensions();
    
    VkResult InitializeInstanceExtensions(VkInstance instance);
    VkResult InitializeDeviceExtensions(VkDevice device);
    void ShutdownExtensions();
    
    PFN_vkVoidFunction GetExtensionProcAddr(const char* name, VkInstance instance = VK_NULL_HANDLE, VkDevice device = VK_NULL_HANDLE);
    bool ShouldInterceptCall(const char* function_name);
    
    void ModifyInstanceCreateInfo(VkInstanceCreateInfo* create_info);
    void ModifyDeviceCreateInfo(VkDeviceCreateInfo* create_info);
    void ModifyPhysicalDeviceFeatures2(VkPhysicalDeviceFeatures2* features);
    void ModifyPhysicalDeviceProperties2(VkPhysicalDeviceProperties2* properties);
    
    void EnableExtension(const std::string& name);
    void DisableExtension(const std::string& name);
    bool IsExtensionEnabled(const std::string& name);
    
    void ListRegisteredExtensions();
    
private:
    ExtensionManager() = default;
    ~ExtensionManager() = default;
    ExtensionManager(const ExtensionManager&) = delete;
    ExtensionManager& operator=(const ExtensionManager&) = delete;
    
    std::vector<std::unique_ptr<BaseExtension>> extensions_;
    std::unordered_map<std::string, BaseExtension*> extension_map_;
    std::unordered_map<std::string, bool> extension_enabled_override_;
    
    VkInstance current_instance_ = VK_NULL_HANDLE;
    VkDevice current_device_ = VK_NULL_HANDLE;
};

} // namespace mali_wrapper