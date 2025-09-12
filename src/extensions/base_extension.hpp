#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>

namespace mali_wrapper {

class BaseExtension {
protected:
    std::string extension_name_;
    uint32_t spec_version_;
    bool enabled_ = false;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    
public:
    virtual ~BaseExtension() = default;
    
    virtual const char* GetName() const = 0;
    virtual uint32_t GetSpecVersion() const = 0;
    virtual std::vector<const char*> GetRequiredDeviceExtensions() const { return {}; }
    virtual std::vector<const char*> GetRequiredInstanceExtensions() const { return {}; }
    
    virtual VkResult Initialize(VkInstance instance, VkDevice device = VK_NULL_HANDLE) {
        instance_ = instance;
        device_ = device;
        enabled_ = true;
        return VK_SUCCESS;
    }
    
    virtual void Shutdown() {
        enabled_ = false;
        instance_ = VK_NULL_HANDLE;
        device_ = VK_NULL_HANDLE;
    }
    
    virtual PFN_vkVoidFunction GetProcAddr(const char* name) { 
        (void)name;
        return nullptr; 
    }
    
    virtual bool InterceptsFunction(const char* name) const = 0;
    
    virtual void ModifyInstanceCreateInfo(VkInstanceCreateInfo* create_info) { 
        (void)create_info;
    }
    
    virtual void ModifyDeviceCreateInfo(VkDeviceCreateInfo* create_info) { 
        (void)create_info;
    }
    
    virtual void ModifyDeviceFeatures(void* features) { 
        (void)features;
    }
    
    virtual void ModifyDeviceProperties(void* properties) { 
        (void)properties;
    }
    
    virtual void ModifyPhysicalDeviceFeatures2(VkPhysicalDeviceFeatures2* features) {
        (void)features;
    }
    
    virtual void ModifyPhysicalDeviceProperties2(VkPhysicalDeviceProperties2* properties) {
        (void)properties;
    }
    
    bool IsEnabled() const { return enabled_; }
    VkInstance GetInstance() const { return instance_; }
    VkDevice GetDevice() const { return device_; }
    
    virtual bool SupportsInstanceLevel() const { return true; }
    virtual bool SupportsDeviceLevel() const { return true; }
};

} // namespace mali_wrapper