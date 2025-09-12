#include "extension_manager.hpp"
#include "../utils/logging.hpp"
#include "../utils/config.hpp"
#include <algorithm>
#include <cstring>

namespace mali_wrapper {

ExtensionManager& ExtensionManager::Instance() {
    static ExtensionManager instance;
    return instance;
}

void ExtensionManager::RegisterExtension(std::unique_ptr<BaseExtension> extension) {
    if (!extension) {
        LOG_ERROR("Attempted to register null extension");
        return;
    }
    
    const char* name = extension->GetName();
    if (!name) {
        LOG_ERROR("Extension has null name");
        return;
    }
    
    LOG_INFO("Registering extension: " + std::string(name));
    
    BaseExtension* ext_ptr = extension.get();
    extension_map_[name] = ext_ptr;
    extensions_.push_back(std::move(extension));
    
    LOG_DEBUG("Extension " + std::string(name) + " registered successfully");
}

BaseExtension* ExtensionManager::GetExtension(const std::string& name) {
    auto it = extension_map_.find(name);
    return (it != extension_map_.end()) ? it->second : nullptr;
}

std::vector<VkExtensionProperties> ExtensionManager::GetSupportedInstanceExtensions() {
    std::vector<VkExtensionProperties> properties;
    
    for (const auto& extension : extensions_) {
        if (!extension->SupportsInstanceLevel()) {
            continue;
        }
        
        std::string ext_name = extension->GetName();
        if (!IsExtensionEnabled(ext_name)) {
            continue;
        }
        
        VkExtensionProperties prop = {};
        strncpy(prop.extensionName, extension->GetName(), VK_MAX_EXTENSION_NAME_SIZE);
        prop.specVersion = extension->GetSpecVersion();
        properties.push_back(prop);
    }
    
    return properties;
}

std::vector<VkExtensionProperties> ExtensionManager::GetSupportedDeviceExtensions() {
    std::vector<VkExtensionProperties> properties;
    
    for (const auto& extension : extensions_) {
        if (!extension->SupportsDeviceLevel()) {
            continue;
        }
        
        std::string ext_name = extension->GetName();
        if (!IsExtensionEnabled(ext_name)) {
            continue;
        }
        
        VkExtensionProperties prop = {};
        strncpy(prop.extensionName, extension->GetName(), VK_MAX_EXTENSION_NAME_SIZE);
        prop.specVersion = extension->GetSpecVersion();
        properties.push_back(prop);
    }
    
    return properties;
}

VkResult ExtensionManager::InitializeInstanceExtensions(VkInstance instance) {
    current_instance_ = instance;
    
    for (const auto& extension : extensions_) {
        if (!extension->SupportsInstanceLevel()) {
            continue;
        }
        
        std::string ext_name = extension->GetName();
        if (!IsExtensionEnabled(ext_name)) {
            LOG_DEBUG("Skipping disabled extension: " + ext_name);
            continue;
        }
        
        LOG_INFO("Initializing instance extension: " + ext_name);
        VkResult result = extension->Initialize(instance);
        if (result != VK_SUCCESS) {
            LOG_ERROR("Failed to initialize extension " + ext_name + ": " + std::to_string(result));
            return result;
        }
    }
    
    return VK_SUCCESS;
}

VkResult ExtensionManager::InitializeDeviceExtensions(VkDevice device) {
    current_device_ = device;
    
    for (const auto& extension : extensions_) {
        if (!extension->SupportsDeviceLevel() || !extension->IsEnabled()) {
            continue;
        }
        
        std::string ext_name = extension->GetName();
        if (!IsExtensionEnabled(ext_name)) {
            continue;
        }
        
        LOG_INFO("Initializing device extension: " + ext_name);
        VkResult result = extension->Initialize(current_instance_, device);
        if (result != VK_SUCCESS) {
            LOG_ERROR("Failed to initialize device extension " + ext_name + ": " + std::to_string(result));
            return result;
        }
    }
    
    return VK_SUCCESS;
}

void ExtensionManager::ShutdownExtensions() {
    LOG_INFO("Shutting down extensions");
    
    for (const auto& extension : extensions_) {
        if (extension->IsEnabled()) {
            extension->Shutdown();
        }
    }
    
    current_instance_ = VK_NULL_HANDLE;
    current_device_ = VK_NULL_HANDLE;
}

PFN_vkVoidFunction ExtensionManager::GetExtensionProcAddr(const char* name, VkInstance instance, VkDevice device) {
    (void)instance;
    (void)device;
    if (!name) {
        return nullptr;
    }
    
    for (const auto& extension : extensions_) {
        if (!extension->IsEnabled()) {
            continue;
        }
        
        if (!IsExtensionEnabled(extension->GetName())) {
            continue;
        }
        
        if (extension->InterceptsFunction(name)) {
            PFN_vkVoidFunction proc = extension->GetProcAddr(name);
            if (proc) {
                LOG_DEBUG("Extension " + std::string(extension->GetName()) + 
                         " provided function: " + std::string(name));
                return proc;
            }
        }
    }
    
    return nullptr;
}

bool ExtensionManager::ShouldInterceptCall(const char* function_name) {
    if (!function_name) {
        return false;
    }
    
    for (const auto& extension : extensions_) {
        if (extension->IsEnabled() && 
            IsExtensionEnabled(extension->GetName()) &&
            extension->InterceptsFunction(function_name)) {
            return true;
        }
    }
    
    return false;
}

void ExtensionManager::ModifyInstanceCreateInfo(VkInstanceCreateInfo* create_info) {
    if (!create_info) {
        return;
    }
    
    for (const auto& extension : extensions_) {
        if (extension->SupportsInstanceLevel() && 
            IsExtensionEnabled(extension->GetName())) {
            extension->ModifyInstanceCreateInfo(create_info);
        }
    }
}

void ExtensionManager::ModifyDeviceCreateInfo(VkDeviceCreateInfo* create_info) {
    if (!create_info) {
        return;
    }
    
    for (const auto& extension : extensions_) {
        if (extension->SupportsDeviceLevel() && 
            IsExtensionEnabled(extension->GetName())) {
            extension->ModifyDeviceCreateInfo(create_info);
        }
    }
}

void ExtensionManager::ModifyPhysicalDeviceFeatures2(VkPhysicalDeviceFeatures2* features) {
    if (!features) {
        return;
    }
    
    for (const auto& extension : extensions_) {
        if (IsExtensionEnabled(extension->GetName())) {
            extension->ModifyPhysicalDeviceFeatures2(features);
        }
    }
}

void ExtensionManager::ModifyPhysicalDeviceProperties2(VkPhysicalDeviceProperties2* properties) {
    if (!properties) {
        return;
    }
    
    for (const auto& extension : extensions_) {
        if (IsExtensionEnabled(extension->GetName())) {
            extension->ModifyPhysicalDeviceProperties2(properties);
        }
    }
}

void ExtensionManager::EnableExtension(const std::string& name) {
    extension_enabled_override_[name] = true;
    LOG_INFO("Extension " + name + " enabled");
}

void ExtensionManager::DisableExtension(const std::string& name) {
    extension_enabled_override_[name] = false;
    LOG_INFO("Extension " + name + " disabled");
}

bool ExtensionManager::IsExtensionEnabled(const std::string& name) {
    auto override_it = extension_enabled_override_.find(name);
    if (override_it != extension_enabled_override_.end()) {
        return override_it->second;
    }
    
    return Config::Instance().IsExtensionEnabled(name);
}

void ExtensionManager::ListRegisteredExtensions() {
    LOG_INFO("Registered extensions:");
    for (const auto& extension : extensions_) {
        std::string status = IsExtensionEnabled(extension->GetName()) ? "enabled" : "disabled";
        LOG_INFO("  " + std::string(extension->GetName()) + " v" + 
                 std::to_string(extension->GetSpecVersion()) + " (" + status + ")");
    }
}

} // namespace mali_wrapper