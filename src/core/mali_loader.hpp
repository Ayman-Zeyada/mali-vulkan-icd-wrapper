#pragma once

#include <vulkan/vulkan.h>
#include <string>

typedef PFN_vkVoidFunction (VKAPI_PTR *PFN_vkGetPhysicalDeviceProcAddr)(VkPhysicalDevice physicalDevice, const char* pName);

namespace mali_wrapper {

class MaliLoader {
public:
    static MaliLoader& Instance();
    
    bool Initialize(const std::string& library_path = "");
    void Shutdown();
    
    bool IsLoaded() const { return library_handle_ != nullptr; }
    
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr() const;
    PFN_vkGetDeviceProcAddr GetDeviceProcAddr() const;
    PFN_vkGetDeviceProcAddr GetDeviceProcAddr(VkInstance instance) const;
    
    template<typename T>
    T GetProcAddr(const char* name) const;
    
    VkResult CreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                           const VkAllocationCallbacks* pAllocator,
                           VkInstance* pInstance);
    
    VkResult EnumerateInstanceExtensionProperties(const char* pLayerName,
                                                 uint32_t* pPropertyCount,
                                                 VkExtensionProperties* pProperties);
    
    VkResult EnumerateInstanceLayerProperties(uint32_t* pPropertyCount,
                                            VkLayerProperties* pProperties);
    
    PFN_vkCreateDevice GetCreateDevice() const;
    
private:
    MaliLoader() = default;
    ~MaliLoader();
    MaliLoader(const MaliLoader&) = delete;
    MaliLoader& operator=(const MaliLoader&) = delete;
    
    void* library_handle_ = nullptr;
    PFN_vkGetInstanceProcAddr get_instance_proc_addr_ = nullptr;
    PFN_vkGetDeviceProcAddr get_device_proc_addr_ = nullptr;
    
    PFN_vkCreateInstance create_instance_ = nullptr;
    PFN_vkCreateDevice create_device_ = nullptr;
    PFN_vkEnumerateInstanceExtensionProperties enumerate_instance_extension_properties_ = nullptr;
    PFN_vkEnumerateInstanceLayerProperties enumerate_instance_layer_properties_ = nullptr;
    
    bool LoadSymbols();
};

template<typename T>
T MaliLoader::GetProcAddr(const char* name) const {
    if (!library_handle_) {
        return nullptr;
    }
    
    if (get_instance_proc_addr_) {
        return reinterpret_cast<T>(get_instance_proc_addr_(VK_NULL_HANDLE, name));
    }
    
    return nullptr;
}

} // namespace mali_wrapper