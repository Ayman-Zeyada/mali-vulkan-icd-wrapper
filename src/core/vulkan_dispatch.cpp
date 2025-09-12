#include "vulkan_dispatch.hpp"
#include "mali_loader.hpp"
#include "extension_manager.hpp"
#include "../utils/logging.hpp"

namespace mali_wrapper {

VulkanDispatch& VulkanDispatch::Instance() {
    static VulkanDispatch instance;
    return instance;
}

void VulkanDispatch::Initialize() {
    LOG_INFO("Initializing Vulkan dispatch system");
}

void VulkanDispatch::Shutdown() {
    LOG_INFO("Shutting down Vulkan dispatch system");
    instance_dispatch_tables_.clear();
    device_dispatch_tables_.clear();
    device_to_instance_.clear();
}

PFN_vkVoidFunction VulkanDispatch::GetInstanceProcAddr(VkInstance instance, const char* pName) {
    if (!pName) {
        return nullptr;
    }
    
    PFN_vkVoidFunction cached_func = GetCachedInstanceFunction(instance, pName);
    if (cached_func) {
        return cached_func;
    }
    
    PFN_vkVoidFunction extension_func = ExtensionManager::Instance().GetExtensionProcAddr(pName, instance);
    if (extension_func) {
        CacheInstanceFunction(instance, pName, extension_func);
        return extension_func;
    }
    
    auto& mali_loader = MaliLoader::Instance();
    if (!mali_loader.IsLoaded()) {
        LOG_ERROR("Mali driver not loaded when requesting function: " + std::string(pName));
        return nullptr;
    }
    
    PFN_vkGetInstanceProcAddr mali_get_instance_proc_addr = mali_loader.GetInstanceProcAddr();
    if (!mali_get_instance_proc_addr) {
        LOG_ERROR("Mali driver vkGetInstanceProcAddr not available");
        return nullptr;
    }
    
    PFN_vkVoidFunction mali_func = mali_get_instance_proc_addr(instance, pName);
    if (mali_func) {
        CacheInstanceFunction(instance, pName, mali_func);
        LOG_DEBUG("Forwarding instance function to Mali driver: " + std::string(pName));
    }
    
    return mali_func;
}

PFN_vkVoidFunction VulkanDispatch::GetDeviceProcAddr(VkDevice device, const char* pName) {
    if (!pName) {
        return nullptr;
    }
    
    PFN_vkVoidFunction cached_func = GetCachedDeviceFunction(device, pName);
    if (cached_func) {
        return cached_func;
    }
    
    PFN_vkVoidFunction extension_func = ExtensionManager::Instance().GetExtensionProcAddr(pName, VK_NULL_HANDLE, device);
    if (extension_func) {
        CacheDeviceFunction(device, pName, extension_func);
        return extension_func;
    }
    
    auto& mali_loader = MaliLoader::Instance();
    if (!mali_loader.IsLoaded()) {
        LOG_ERROR("Mali driver not loaded when requesting device function: " + std::string(pName));
        return nullptr;
    }
    
    // Get the device proc addr using the correct instance
    VkInstance device_instance = VK_NULL_HANDLE;
    auto device_instance_it = device_to_instance_.find(device);
    if (device_instance_it != device_to_instance_.end()) {
        device_instance = device_instance_it->second;
    }
    
    PFN_vkGetDeviceProcAddr mali_get_device_proc_addr = mali_loader.GetDeviceProcAddr(device_instance);
    if (!mali_get_device_proc_addr) {
        LOG_ERROR("Mali driver vkGetDeviceProcAddr not available");
        return nullptr;
    }
    
    PFN_vkVoidFunction mali_func = mali_get_device_proc_addr(device, pName);
    if (mali_func) {
        CacheDeviceFunction(device, pName, mali_func);
        LOG_DEBUG("Forwarding device function to Mali driver: " + std::string(pName));
    }
    
    return mali_func;
}

void VulkanDispatch::RegisterInstance(VkInstance instance) {
    if (instance != VK_NULL_HANDLE) {
        instance_dispatch_tables_[instance] = {};
        LOG_DEBUG("Registered Vulkan instance");
    }
}

void VulkanDispatch::RegisterDevice(VkDevice device, VkInstance instance) {
    if (device != VK_NULL_HANDLE) {
        device_dispatch_tables_[device] = {};
        if (instance != VK_NULL_HANDLE) {
            device_to_instance_[device] = instance;
        }
        LOG_DEBUG("Registered Vulkan device");
    }
}

void VulkanDispatch::UnregisterInstance(VkInstance instance) {
    try {
        auto it = instance_dispatch_tables_.find(instance);
        if (it != instance_dispatch_tables_.end()) {
            instance_dispatch_tables_.erase(it);
            LOG_DEBUG("Unregistered Vulkan instance");
        }
    } catch (...) {
        // Ignore errors during cleanup to prevent hanging
    }
}

void VulkanDispatch::UnregisterDevice(VkDevice device) {
    auto it = device_dispatch_tables_.find(device);
    if (it != device_dispatch_tables_.end()) {
        device_dispatch_tables_.erase(it);
    }
    
    auto instance_it = device_to_instance_.find(device);
    if (instance_it != device_to_instance_.end()) {
        device_to_instance_.erase(instance_it);
    }
    
    LOG_DEBUG("Unregistered Vulkan device");
}

PFN_vkVoidFunction VulkanDispatch::GetCachedInstanceFunction(VkInstance instance, const char* name) {
    auto instance_it = instance_dispatch_tables_.find(instance);
    if (instance_it != instance_dispatch_tables_.end()) {
        auto func_it = instance_it->second.find(name);
        if (func_it != instance_it->second.end()) {
            return func_it->second;
        }
    }
    return nullptr;
}

PFN_vkVoidFunction VulkanDispatch::GetCachedDeviceFunction(VkDevice device, const char* name) {
    auto device_it = device_dispatch_tables_.find(device);
    if (device_it != device_dispatch_tables_.end()) {
        auto func_it = device_it->second.find(name);
        if (func_it != device_it->second.end()) {
            return func_it->second;
        }
    }
    return nullptr;
}

void VulkanDispatch::CacheInstanceFunction(VkInstance instance, const char* name, PFN_vkVoidFunction func) {
    instance_dispatch_tables_[instance][name] = func;
}

void VulkanDispatch::CacheDeviceFunction(VkDevice device, const char* name, PFN_vkVoidFunction func) {
    device_dispatch_tables_[device][name] = func;
}

} // namespace mali_wrapper