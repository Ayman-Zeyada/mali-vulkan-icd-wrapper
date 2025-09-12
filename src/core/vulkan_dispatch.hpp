#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <string>

namespace mali_wrapper {

class VulkanDispatch {
public:
    static VulkanDispatch& Instance();
    
    void Initialize();
    void Shutdown();
    
    PFN_vkVoidFunction GetInstanceProcAddr(VkInstance instance, const char* pName);
    PFN_vkVoidFunction GetDeviceProcAddr(VkDevice device, const char* pName);
    
    void RegisterInstance(VkInstance instance);
    void RegisterDevice(VkDevice device, VkInstance instance = VK_NULL_HANDLE);
    void UnregisterInstance(VkInstance instance);
    void UnregisterDevice(VkDevice device);
    
    template<typename T>
    T GetInstanceFunction(VkInstance instance, const char* name);
    
    template<typename T>
    T GetDeviceFunction(VkDevice device, const char* name);
    
private:
    VulkanDispatch() = default;
    ~VulkanDispatch() = default;
    VulkanDispatch(const VulkanDispatch&) = delete;
    VulkanDispatch& operator=(const VulkanDispatch&) = delete;
    
    std::unordered_map<VkInstance, std::unordered_map<std::string, PFN_vkVoidFunction>> instance_dispatch_tables_;
    std::unordered_map<VkDevice, std::unordered_map<std::string, PFN_vkVoidFunction>> device_dispatch_tables_;
    std::unordered_map<VkDevice, VkInstance> device_to_instance_;
    
    PFN_vkVoidFunction GetCachedInstanceFunction(VkInstance instance, const char* name);
    PFN_vkVoidFunction GetCachedDeviceFunction(VkDevice device, const char* name);
    void CacheInstanceFunction(VkInstance instance, const char* name, PFN_vkVoidFunction func);
    void CacheDeviceFunction(VkDevice device, const char* name, PFN_vkVoidFunction func);
};

template<typename T>
T VulkanDispatch::GetInstanceFunction(VkInstance instance, const char* name) {
    return reinterpret_cast<T>(GetInstanceProcAddr(instance, name));
}

template<typename T>
T VulkanDispatch::GetDeviceFunction(VkDevice device, const char* name) {
    return reinterpret_cast<T>(GetDeviceProcAddr(device, name));
}

} // namespace mali_wrapper