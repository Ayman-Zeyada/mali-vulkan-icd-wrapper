#include "mali_loader.hpp"
#include "../utils/logging.hpp"
#include "../utils/config.hpp"
#include <dlfcn.h>

namespace mali_wrapper {

MaliLoader& MaliLoader::Instance() {
    static MaliLoader instance;
    return instance;
}

MaliLoader::~MaliLoader() {
    Shutdown();
}

bool MaliLoader::Initialize(const std::string& library_path) {
    if (library_handle_) {
        LOG_WARN("MaliLoader already initialized");
        return true;
    }
    
    std::string mali_path = library_path;
    if (mali_path.empty()) {
        mali_path = Config::Instance().GetMaliDriverPath();
    }
    
    LOG_INFO("Loading Mali driver from: " + mali_path);
    
    library_handle_ = dlopen(mali_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library_handle_) {
        LOG_ERROR("Failed to load Mali driver: " + std::string(dlerror()));
        return false;
    }
    
    if (!LoadSymbols()) {
        LOG_ERROR("Failed to load required symbols from Mali driver");
        Shutdown();
        return false;
    }
    
    LOG_INFO("Mali driver loaded successfully");
    return true;
}

void MaliLoader::Shutdown() {
    if (library_handle_) {
        dlclose(library_handle_);
        library_handle_ = nullptr;
        get_instance_proc_addr_ = nullptr;
        get_device_proc_addr_ = nullptr;
        create_instance_ = nullptr;
        create_device_ = nullptr;
        enumerate_instance_extension_properties_ = nullptr;
        enumerate_instance_layer_properties_ = nullptr;
        
        LOG_INFO("Mali driver unloaded");
    }
}

bool MaliLoader::LoadSymbols() {
    get_instance_proc_addr_ = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(library_handle_, "vk_icdGetInstanceProcAddr"));
    if (!get_instance_proc_addr_) {
        get_instance_proc_addr_ = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            dlsym(library_handle_, "vkGetInstanceProcAddr"));
    }
    if (!get_instance_proc_addr_) {
        LOG_ERROR("Failed to load vkGetInstanceProcAddr or vk_icdGetInstanceProcAddr");
        return false;
    }
    
    get_device_proc_addr_ = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        dlsym(library_handle_, "vkGetDeviceProcAddr"));
    if (!get_device_proc_addr_ && get_instance_proc_addr_) {
        get_device_proc_addr_ = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            get_instance_proc_addr_(VK_NULL_HANDLE, "vkGetDeviceProcAddr"));
    }
    
    if (!get_device_proc_addr_) {
        LOG_WARN("vkGetDeviceProcAddr not found directly, will obtain it dynamically");
        get_device_proc_addr_ = nullptr;
    }
    
    create_instance_ = GetProcAddr<PFN_vkCreateInstance>("vkCreateInstance");
    if (!create_instance_) {
        LOG_ERROR("Failed to load vkCreateInstance");
        return false;
    }
    
    // Note: vkCreateDevice is an instance function and cannot be loaded with VK_NULL_HANDLE
    // We'll load it dynamically when needed using a valid instance
    create_device_ = nullptr;
    
    enumerate_instance_extension_properties_ = GetProcAddr<PFN_vkEnumerateInstanceExtensionProperties>(
        "vkEnumerateInstanceExtensionProperties");
    if (!enumerate_instance_extension_properties_) {
        LOG_ERROR("Failed to load vkEnumerateInstanceExtensionProperties");
        return false;
    }
    
    enumerate_instance_layer_properties_ = GetProcAddr<PFN_vkEnumerateInstanceLayerProperties>(
        "vkEnumerateInstanceLayerProperties");
    if (!enumerate_instance_layer_properties_) {
        LOG_ERROR("Failed to load vkEnumerateInstanceLayerProperties");
        return false;
    }
    
    return true;
}

PFN_vkGetInstanceProcAddr MaliLoader::GetInstanceProcAddr() const {
    return get_instance_proc_addr_;
}

PFN_vkGetDeviceProcAddr MaliLoader::GetDeviceProcAddr() const {
    return get_device_proc_addr_;
}

PFN_vkGetDeviceProcAddr MaliLoader::GetDeviceProcAddr(VkInstance instance) const {
    if (!get_device_proc_addr_ && get_instance_proc_addr_ && instance != VK_NULL_HANDLE) {
        return reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            get_instance_proc_addr_(instance, "vkGetDeviceProcAddr"));
    }
    return get_device_proc_addr_;
}

VkResult MaliLoader::CreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                   const VkAllocationCallbacks* pAllocator,
                                   VkInstance* pInstance) {
    if (!create_instance_) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    return create_instance_(pCreateInfo, pAllocator, pInstance);
}

VkResult MaliLoader::EnumerateInstanceExtensionProperties(const char* pLayerName,
                                                         uint32_t* pPropertyCount,
                                                         VkExtensionProperties* pProperties) {
    if (!enumerate_instance_extension_properties_) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    return enumerate_instance_extension_properties_(pLayerName, pPropertyCount, pProperties);
}

VkResult MaliLoader::EnumerateInstanceLayerProperties(uint32_t* pPropertyCount,
                                                    VkLayerProperties* pProperties) {
    if (!enumerate_instance_layer_properties_) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    return enumerate_instance_layer_properties_(pPropertyCount, pProperties);
}

PFN_vkCreateDevice MaliLoader::GetCreateDevice() const {
    // vkCreateDevice should be obtained dynamically using a valid instance
    // This method is deprecated and should not be used
    return nullptr;
}

} // namespace mali_wrapper