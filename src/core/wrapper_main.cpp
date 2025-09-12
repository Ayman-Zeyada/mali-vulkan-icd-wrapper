#include <vulkan/vulkan.h>
#include "mali_loader.hpp"
#include "extension_manager.hpp"
#include "vulkan_dispatch.hpp"
#include "../utils/logging.hpp"
#include "../utils/config.hpp"
#include "../extensions/map_memory_placed/map_memory_placed.hpp"
#include <mutex>
#include <atomic>
#include <cstring>

using namespace mali_wrapper;

static std::mutex g_init_mutex;
static bool g_initialized = false;
static std::atomic<int> g_instance_count{0};
static VkInstance g_current_instance = VK_NULL_HANDLE;

static bool InitializeWrapper() {
    std::lock_guard<std::mutex> lock(g_init_mutex);
    
    if (g_initialized) {
        return true;
    }
    
    if (!Config::Instance().LoadFromFile("/etc/mali-wrapper/extensions.conf")) {
        if (!Config::Instance().LoadFromFile("/tmp/mali-wrapper/extensions.conf")) {
            Config::Instance().LoadFromFile("config/extensions.conf");
        }
    }
    Config::Instance().LoadFromEnvironment();
    
    std::string log_level_str = Config::Instance().GetValue("logging", "level", "info");
    LogLevel log_level = LogLevel::INFO;
    if (log_level_str == "debug") log_level = LogLevel::DEBUG;
    else if (log_level_str == "warn") log_level = LogLevel::WARN;
    else if (log_level_str == "error") log_level = LogLevel::ERROR;
    
    Logger::Instance().SetLevel(log_level);
    Logger::Instance().EnableConsole(Config::Instance().GetBoolValue("logging", "enable_console", true));
    
    std::string log_file = Config::Instance().GetValue("logging", "output");
    if (!log_file.empty()) {
        Logger::Instance().SetOutputFile(log_file);
    }
    
    LOG_INFO("Mali Extension Wrapper initializing...");
    
    if (!MaliLoader::Instance().Initialize()) {
        LOG_ERROR("Failed to initialize Mali loader");
        return false;
    }
    
    VulkanDispatch::Instance().Initialize();
    
    ExtensionManager::Instance().RegisterExtension(std::make_unique<MapMemoryPlacedExtension>());
    
    ExtensionManager::Instance().ListRegisteredExtensions();
    
    g_initialized = true;
    LOG_INFO("Mali Extension Wrapper initialized successfully");
    return true;
}


extern "C" {


VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance) {
    
    if (!InitializeWrapper()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    if (!pCreateInfo || !pInstance) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    VkInstanceCreateInfo modified_create_info = *pCreateInfo;
    ExtensionManager::Instance().ModifyInstanceCreateInfo(&modified_create_info);
    
    LOG_INFO("Creating Vulkan instance");
    VkResult result = MaliLoader::Instance().CreateInstance(&modified_create_info, pAllocator, pInstance);
    
    if (result == VK_SUCCESS && *pInstance != VK_NULL_HANDLE) {
        VulkanDispatch::Instance().RegisterInstance(*pInstance);
        ExtensionManager::Instance().InitializeInstanceExtensions(*pInstance);
        g_instance_count.fetch_add(1);
        g_current_instance = *pInstance; // Store the current instance for device creation
        LOG_INFO("Vulkan instance created successfully (count: " + std::to_string(g_instance_count.load()) + ")");
    } else {
        LOG_ERROR("Failed to create Vulkan instance: " + std::to_string(result));
    }
    
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(
    VkInstance instance,
    const VkAllocationCallbacks* pAllocator) {
    
    if (instance == VK_NULL_HANDLE) {
        return;
    }
    
    // Clean up our wrapper state but avoid Mali driver calls that might hang
    VulkanDispatch::Instance().UnregisterInstance(instance);
    g_instance_count.fetch_sub(1);
    
    // Skip calling Mali driver's vkDestroyInstance to avoid hanging
    // The process will clean up the Mali driver resources on exit
    (void)pAllocator;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice) {
    
    LOG_INFO("vkCreateDevice called in wrapper");
    
    if (!pCreateInfo || !pDevice) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    VkDeviceCreateInfo modified_create_info = *pCreateInfo;
    ExtensionManager::Instance().ModifyDeviceCreateInfo(&modified_create_info);
    
    // Get vkCreateDevice dynamically using the current instance
    // vkCreateDevice is an instance function and must be obtained with a valid instance
    PFN_vkCreateDevice create_device = nullptr;
    auto mali_get_instance_proc_addr = MaliLoader::Instance().GetInstanceProcAddr();
    if (mali_get_instance_proc_addr && g_current_instance != VK_NULL_HANDLE) {
        create_device = reinterpret_cast<PFN_vkCreateDevice>(
            mali_get_instance_proc_addr(g_current_instance, "vkCreateDevice"));
        LOG_INFO("vkCreateDevice from Mali driver: " + std::to_string(reinterpret_cast<uintptr_t>(create_device)));
    } else {
        LOG_ERROR("Cannot get vkCreateDevice: missing instance proc addr or null instance");
    }
    
    if (!create_device) {
        LOG_ERROR("vkCreateDevice not available from Mali driver");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    LOG_INFO("Creating Vulkan device");
    VkResult result = create_device(physicalDevice, &modified_create_info, pAllocator, pDevice);
    
    if (result == VK_SUCCESS && *pDevice != VK_NULL_HANDLE) {
        VulkanDispatch::Instance().RegisterDevice(*pDevice, g_current_instance);
        ExtensionManager::Instance().InitializeDeviceExtensions(*pDevice);
        LOG_INFO("Vulkan device created successfully");
    } else {
        LOG_ERROR("Failed to create Vulkan device: " + std::to_string(result));
    }
    
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(
    VkDevice device,
    const VkAllocationCallbacks* pAllocator) {
    
    if (device == VK_NULL_HANDLE) {
        return;
    }
    
    LOG_INFO("Destroying Vulkan device");
    
    VulkanDispatch::Instance().UnregisterDevice(device);
    
    auto destroy_device = VulkanDispatch::Instance().GetDeviceFunction<PFN_vkDestroyDevice>(
        device, "vkDestroyDevice");
    
    if (destroy_device) {
        destroy_device(device, pAllocator);
    }
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance instance,
    const char* pName) {
    
    if (!pName) {
        return nullptr;
    }
    
    
    if (!InitializeWrapper()) {
        return nullptr;
    }
    
    if (strcmp(pName, "vkCreateInstance") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateInstance);
    }
    if (strcmp(pName, "vkDestroyInstance") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyInstance);
    }
    if (strcmp(pName, "vkGetInstanceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetInstanceProcAddr);
    }
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    }
    if (strcmp(pName, "vkCreateDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateDevice);
    }
    if (strcmp(pName, "vkDestroyDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyDevice);
    }
    
    // For all other functions, forward to Mali driver
    PFN_vkVoidFunction result = VulkanDispatch::Instance().GetInstanceProcAddr(instance, pName);
    
    return result;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
    VkDevice device,
    const char* pName) {
    
    if (!pName) {
        return nullptr;
    }
    
    if (!InitializeWrapper()) {
        return nullptr;
    }
    
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    }
    if (strcmp(pName, "vkDestroyDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyDevice);
    }
    
    return VulkanDispatch::Instance().GetDeviceProcAddr(device, pName);
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties) {
    
    if (!InitializeWrapper()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    if (!pPropertyCount) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    VkResult mali_result = MaliLoader::Instance().EnumerateInstanceExtensionProperties(
        pLayerName, pPropertyCount, pProperties);
    
    if (mali_result != VK_SUCCESS && mali_result != VK_INCOMPLETE) {
        return mali_result;
    }
    
    auto wrapper_extensions = ExtensionManager::Instance().GetSupportedInstanceExtensions();
    
    if (!pProperties) {
        *pPropertyCount += static_cast<uint32_t>(wrapper_extensions.size());
        return mali_result;
    }
    
    uint32_t original_count = *pPropertyCount;
    uint32_t total_extensions = original_count + static_cast<uint32_t>(wrapper_extensions.size());
    
    for (size_t i = 0; i < wrapper_extensions.size() && (original_count + i) < total_extensions; ++i) {
        if (original_count + i < *pPropertyCount) {
            pProperties[original_count + i] = wrapper_extensions[i];
        }
    }
    
    *pPropertyCount = total_extensions;
    
    return (*pPropertyCount > total_extensions) ? VK_INCOMPLETE : VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(
    uint32_t* pPropertyCount,
    VkLayerProperties* pProperties) {
    
    if (!InitializeWrapper()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    return MaliLoader::Instance().EnumerateInstanceLayerProperties(pPropertyCount, pProperties);
}

} // extern "C"