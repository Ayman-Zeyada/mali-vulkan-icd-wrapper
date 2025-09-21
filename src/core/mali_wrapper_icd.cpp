#include "mali_wrapper_icd.hpp"
#include "library_loader.hpp"
#include "wsi_manager.hpp"
#include "wsi/wsi_private_data.hpp"
#include "wsi/wsi_factory.hpp"
#include "wsi/layer_utils/extension_list.hpp"
#include <vulkan/vk_icd.h>
#include "config.hpp"
#include "../utils/logging.hpp"
#include <cstring>
#include <unordered_set>
#include <unordered_map>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <dlfcn.h>
#include <cstdio>
#include <memory>
#include <string>
#include <chrono>
#include <mutex>

namespace mali_wrapper {

struct InstanceInfo {
    VkInstance instance;
    int ref_count;
    std::chrono::steady_clock::time_point destroy_time;
    bool marked_for_destruction;

    InstanceInfo(VkInstance inst) : instance(inst), ref_count(1), marked_for_destruction(false) {}
};

static std::unordered_map<VkInstance, std::unique_ptr<InstanceInfo>> managed_instances;
static std::unordered_map<VkDevice, VkInstance> managed_devices;
static std::mutex instance_mutex;
static VkInstance latest_instance = VK_NULL_HANDLE;

void add_instance_reference(VkInstance instance) {
    std::lock_guard<std::mutex> lock(instance_mutex);
    auto it = managed_instances.find(instance);
    if (it != managed_instances.end()) {
        it->second->ref_count++;
    }
}

void remove_instance_reference(VkInstance instance) {
    bool should_cleanup = false;
    {
        std::lock_guard<std::mutex> lock(instance_mutex);
        auto it = managed_instances.find(instance);
        if (it != managed_instances.end()) {
            it->second->ref_count--;

            if (it->second->marked_for_destruction && it->second->ref_count <= 0) {
                should_cleanup = true;
                managed_instances.erase(it);
            }
        }
    }

    if (should_cleanup) {
        LOG_INFO("Performing delayed instance cleanup for instance with 0 references");
        GetWSIManager().release_instance(instance);
    }
}

bool is_instance_valid(VkInstance instance) {
    std::lock_guard<std::mutex> lock(instance_mutex);
    auto it = managed_instances.find(instance);
    return (it != managed_instances.end() && !it->second->marked_for_destruction);
}

static VkInstance get_device_parent_instance(VkDevice device)
{
    auto it = managed_devices.find(device);
    if (it != managed_devices.end())
    {
        return it->second;
    }

    std::lock_guard<std::mutex> lock(instance_mutex);
    if (latest_instance != VK_NULL_HANDLE)
    {
        auto latest_it = managed_instances.find(latest_instance);
        if (latest_it != managed_instances.end())
        {
            return latest_it->second->instance;
        }
    }

    if (!managed_instances.empty())
    {
        return managed_instances.begin()->second->instance;
    }

    return VK_NULL_HANDLE;
}


static const std::unordered_set<std::string> wsi_functions = {
    // Surface functions
    "vkCreateXlibSurfaceKHR",
    "vkCreateXcbSurfaceKHR",
    "vkCreateWaylandSurfaceKHR",
    "vkCreateDisplaySurfaceKHR",
    "vkCreateHeadlessSurfaceEXT",
    "vkDestroySurfaceKHR",
    "vkGetPhysicalDeviceSurfaceSupportKHR",
    "vkGetPhysicalDeviceSurfaceCapabilitiesKHR",
    "vkGetPhysicalDeviceSurfaceCapabilities2KHR",
    "vkGetPhysicalDeviceSurfaceFormatsKHR",
    "vkGetPhysicalDeviceSurfaceFormats2KHR",
    "vkGetPhysicalDeviceSurfacePresentModesKHR",

    // Swapchain functions
    "vkCreateSwapchainKHR",
    "vkCreateSharedSwapchainsKHR",
    "vkDestroySwapchainKHR",
    "vkGetSwapchainImagesKHR",
    "vkAcquireNextImageKHR",
    "vkAcquireNextImage2KHR",
    "vkQueuePresentKHR",
    "vkGetSwapchainStatusKHR",
    "vkReleaseSwapchainImagesEXT",

    // Display functions
    "vkGetPhysicalDeviceDisplayPropertiesKHR",
    "vkGetPhysicalDeviceDisplayProperties2KHR",
    "vkGetPhysicalDeviceDisplayPlanePropertiesKHR",
    "vkGetPhysicalDeviceDisplayPlaneProperties2KHR",
    "vkGetDisplayPlaneSupportedDisplaysKHR",
    "vkGetDisplayModePropertiesKHR",
    "vkGetDisplayModeProperties2KHR",
    "vkCreateDisplayModeKHR",
    "vkGetDisplayPlaneCapabilitiesKHR",
    "vkGetDisplayPlaneCapabilities2KHR",

    // Present timing functions
    "vkGetSwapchainTimingPropertiesEXT",
    "vkGetSwapchainTimeDomainPropertiesEXT",
    "vkGetPastPresentationTimingEXT",
    "vkSetSwapchainPresentTimingQueueSizeEXT",

    // Presentation support functions
    "vkGetPhysicalDeviceWaylandPresentationSupportKHR",
    "vkGetPhysicalDeviceXlibPresentationSupportKHR",
    "vkGetPhysicalDeviceXcbPresentationSupportKHR"
};

static bool IsWSIFunction(const char* name) {
    return wsi_functions.find(name) != wsi_functions.end();
}

bool InitializeWrapper() {
    if (getenv("MALI_WRAPPER_DEBUG")) {
        Logger::Instance().SetLevel(LogLevel::DEBUG);
        }

    LOG_INFO("Initializing Mali Wrapper ICD");

    if (!LibraryLoader::Instance().LoadLibraries()) {
        LOG_ERROR("Failed to load required libraries - continuing with reduced functionality");
        LOG_WARN("Extension enumeration and WSI functionality may be limited");
    }

    LOG_INFO("Mali Wrapper ICD initialized successfully");
    return true;
}

void ShutdownWrapper() {
    LOG_INFO("Shutting down Mali Wrapper ICD");
    GetWSIManager().cleanup();
    LibraryLoader::Instance().UnloadLibraries();
}


} // namespace mali_wrapper

static VKAPI_ATTR VkResult VKAPI_CALL dummy_set_instance_loader_data(VkInstance instance, void* object) {
    return VK_SUCCESS;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL internal_vkGetDeviceProcAddr(VkDevice device, const char* pName);
static VKAPI_ATTR VkResult VKAPI_CALL internal_vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice);
static VKAPI_ATTR void VKAPI_CALL internal_vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator);
static VKAPI_ATTR VkResult VKAPI_CALL mali_driver_create_device(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice);

static VKAPI_ATTR VkResult VKAPI_CALL wrapper_vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pSwapchainCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain);

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL filtered_mali_get_instance_proc_addr(VkInstance instance, const char* pName) {
    using namespace mali_wrapper;

    if (!pName) {
        return nullptr;
    }


    if (IsWSIFunction(pName)) {
        return nullptr;
    }

    if (strcmp(pName, "vkCreateDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(mali_driver_create_device);
    }

    auto mali_proc_addr = LibraryLoader::Instance().GetMaliGetInstanceProcAddr();
    if (mali_proc_addr) {
        auto func = mali_proc_addr(instance, pName);
        return func;
    }

    return nullptr;
}

static VKAPI_ATTR VkResult VKAPI_CALL internal_vkCreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance) {

    using namespace mali_wrapper;


    if (!pCreateInfo || !pInstance) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<const char *> enabled_extensions;
    std::unique_ptr<util::extension_list> instance_extension_list;
    util::wsi_platform_set enabled_platforms;

#if BUILD_WSI_X11
    enabled_platforms.add(VK_ICD_WSI_PLATFORM_XCB);
    enabled_platforms.add(VK_ICD_WSI_PLATFORM_XLIB);
#endif
#if BUILD_WSI_WAYLAND
    enabled_platforms.add(VK_ICD_WSI_PLATFORM_WAYLAND);
#endif
#if BUILD_WSI_HEADLESS
    enabled_platforms.add(VK_ICD_WSI_PLATFORM_HEADLESS);
#endif

    try
    {
        util::allocator base_allocator = util::allocator::get_generic();
        util::allocator extension_allocator(base_allocator, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
        instance_extension_list = std::make_unique<util::extension_list>(extension_allocator);
        auto &extensions = *instance_extension_list;

        if (pCreateInfo->enabledExtensionCount > 0 && pCreateInfo->ppEnabledExtensionNames != nullptr)
        {
            extensions.add(pCreateInfo->ppEnabledExtensionNames, pCreateInfo->enabledExtensionCount);
        }

        VkResult extension_result = wsi::add_instance_extensions_required_by_layer(enabled_platforms, extensions);
        if (extension_result != VK_SUCCESS)
        {
            LOG_ERROR("Failed to collect WSI-required instance extensions, error: " +
                      std::to_string(extension_result));
            return extension_result;
        }

        util::vector<const char *> extension_vector(extension_allocator);
        extensions.get_extension_strings(extension_vector);

        std::unordered_set<std::string> seen_extensions;
        seen_extensions.reserve(extension_vector.size());

        for (const char *name : extension_vector)
        {
            if (name == nullptr)
            {
                continue;
            }
            auto inserted = seen_extensions.emplace(name);
            if (inserted.second)
            {
                enabled_extensions.push_back(name);
            }
        }
    }
    catch (const std::exception &e)
    {
        LOG_WARN(std::string("Unable to augment instance extensions: ") + e.what());
        enabled_extensions.clear();
        instance_extension_list.reset();
    }

    const char *const *instance_extension_ptr = pCreateInfo->ppEnabledExtensionNames;
    size_t instance_extension_count = pCreateInfo->enabledExtensionCount;

    if (!enabled_extensions.empty())
    {
        instance_extension_ptr = enabled_extensions.data();
        instance_extension_count = enabled_extensions.size();
    }

    VkInstanceCreateInfo modified_create_info = *pCreateInfo;
    modified_create_info.enabledExtensionCount = static_cast<uint32_t>(instance_extension_count);
    modified_create_info.ppEnabledExtensionNames = instance_extension_ptr;

    auto mali_create_instance = LibraryLoader::Instance().GetMaliCreateInstance();
    if (!mali_create_instance) {
        LOG_ERROR("Mali driver not available for instance creation");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = mali_create_instance(&modified_create_info, pAllocator, pInstance);

    if (result == VK_SUCCESS) {
        std::lock_guard<std::mutex> lock(instance_mutex);
        auto existing = managed_instances.find(*pInstance);
        if (existing == managed_instances.end()) {
            managed_instances.emplace(*pInstance, std::make_unique<InstanceInfo>(*pInstance));
        } else {
            LOG_WARN("Instance handle reused - resetting tracking state");
            existing->second->instance = *pInstance;
            existing->second->ref_count = 1;
            existing->second->marked_for_destruction = false;
            existing->second->destroy_time = {};
        }
        latest_instance = *pInstance;

        VkResult wsi_result = GetWSIManager().initialize(*pInstance, VK_NULL_HANDLE);
        if (wsi_result != VK_SUCCESS) {
            LOG_ERROR("Failed to initialize WSI manager for instance, error: " + std::to_string(wsi_result));
        }

        try
        {
            auto &instance_data = instance_private_data::get(*pInstance);
            if (instance_extension_ptr != nullptr && instance_extension_count > 0)
            {
                instance_data.set_instance_enabled_extensions(instance_extension_ptr, instance_extension_count);
            }
        }
        catch (const std::exception &e)
        {
            LOG_WARN(std::string("Failed to record enabled instance extensions: ") + e.what());
        }

        LOG_INFO("Instance created successfully through WSI layer -> Mali driver chain");
    } else {
        LOG_ERROR("Failed to create instance through WSI layer, error: " + std::to_string(result));
    }

    return result;
}

static VKAPI_ATTR void VKAPI_CALL internal_vkDestroyInstance(
    VkInstance instance,
    const VkAllocationCallbacks* pAllocator) {

    using namespace mali_wrapper;


    if (instance == VK_NULL_HANDLE) {
        return;
    }

    std::unique_ptr<InstanceInfo> instance_info;
    {
        std::lock_guard<std::mutex> lock(instance_mutex);
        auto it = managed_instances.find(instance);
        if (it == managed_instances.end()) {
            LOG_WARN("Destroying unmanaged instance");
            return;
        }

        it->second->marked_for_destruction = true;
        it->second->destroy_time = std::chrono::steady_clock::now();

        LOG_INFO("Instance marked for destruction with ref_count=" + std::to_string(it->second->ref_count));

        if (it->second->ref_count > 0) {
            LOG_WARN("Instance has " + std::to_string(it->second->ref_count) +
                     " active references - deferring cleanup to prevent race conditions");
            return; // Don't destroy yet, let reference cleanup handle it
        }

        instance_info = std::move(it->second);
        managed_instances.erase(it);
        if (latest_instance == instance) {
            latest_instance = VK_NULL_HANDLE;
            if (!managed_instances.empty()) {
                latest_instance = managed_instances.begin()->second->instance;
            }
        }
    }

    // Cleanup associated devices
    for (auto dev_it = managed_devices.begin(); dev_it != managed_devices.end(); ) {
        if (dev_it->second == instance) {
            GetWSIManager().release_device(dev_it->first);
            dev_it = managed_devices.erase(dev_it);
        } else {
            ++dev_it;
        }
    }

    auto mali_proc_addr = LibraryLoader::Instance().GetMaliGetInstanceProcAddr();
    if (mali_proc_addr) {
        auto mali_destroy = reinterpret_cast<PFN_vkDestroyInstance>(
            mali_proc_addr(instance, "vkDestroyInstance"));
        if (mali_destroy) {
            mali_destroy(instance, pAllocator);
        }
    }

    GetWSIManager().release_instance(instance);
    LOG_INFO("Instance destroyed successfully");
}

static VKAPI_ATTR VkResult VKAPI_CALL internal_vkEnumerateInstanceExtensionProperties(
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties) {

    using namespace mali_wrapper;


    if (pLayerName != nullptr) {
        *pPropertyCount = 0;
        return VK_SUCCESS;
    }

    std::vector<VkExtensionProperties> mali_extensions;

    if (LibraryLoader::Instance().IsLoaded()) {
        auto mali_enumerate = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
            LibraryLoader::Instance().GetMaliProcAddr("vkEnumerateInstanceExtensionProperties"));
        if (mali_enumerate) {
            uint32_t mali_count = 0;
            VkResult result = mali_enumerate(nullptr, &mali_count, nullptr);
            if (result == VK_SUCCESS && mali_count > 0) {
                mali_extensions.resize(mali_count);
                mali_enumerate(nullptr, &mali_count, mali_extensions.data());
            }
        }
    }

    std::vector<VkExtensionProperties> wsi_extensions;
    bool wsi_available = false;
    try {
        wsi_available = LibraryLoader::Instance().IsLoaded();
    } catch (...) {
        LOG_WARN("Exception checking WSI availability during extension enumeration");
        wsi_available = false;
    }

    if (wsi_available) {
        const char* wsi_extension_names[] = {
            "VK_KHR_surface",
            "VK_KHR_wayland_surface",
            "VK_KHR_xcb_surface",
            "VK_KHR_xlib_surface",
            "VK_KHR_get_surface_capabilities2",
            "VK_EXT_surface_maintenance1",
            "VK_EXT_headless_surface"
        };

        for (const char* ext_name : wsi_extension_names) {
            VkExtensionProperties ext = {};
            strncpy(ext.extensionName, ext_name, VK_MAX_EXTENSION_NAME_SIZE - 1);
            ext.specVersion = 1;  // Default spec version
            wsi_extensions.push_back(ext);
        }

    }

    std::vector<VkExtensionProperties> combined_extensions = mali_extensions;
    for (const auto& wsi_ext : wsi_extensions) {
        bool found = false;
        for (const auto& mali_ext : mali_extensions) {
            if (strcmp(wsi_ext.extensionName, mali_ext.extensionName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            combined_extensions.push_back(wsi_ext);
        }
    }


    if (pProperties == nullptr) {
        *pPropertyCount = combined_extensions.size();
        return VK_SUCCESS;
    }

    uint32_t copy_count = std::min(*pPropertyCount, static_cast<uint32_t>(combined_extensions.size()));
    for (uint32_t i = 0; i < copy_count; i++) {
        pProperties[i] = combined_extensions[i];
    }

    *pPropertyCount = copy_count;
    return copy_count < combined_extensions.size() ? VK_INCOMPLETE : VK_SUCCESS;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL internal_vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    using namespace mali_wrapper;

    if (!pName) {
        return nullptr;
    }


    if (strcmp(pName, "vkGetInstanceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(internal_vkGetInstanceProcAddr);
    }

    if (strcmp(pName, "vkCreateInstance") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(internal_vkCreateInstance);
    }

    if (strcmp(pName, "vkDestroyInstance") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(internal_vkDestroyInstance);
    }

    if (strcmp(pName, "vkDestroyDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(internal_vkDestroyDevice);
    }

    if (strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(internal_vkEnumerateInstanceExtensionProperties);
    }

    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(internal_vkGetDeviceProcAddr);
    }

    if (strcmp(pName, "vkCreateDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(internal_vkCreateDevice);
    }

    if (GetWSIManager().is_wsi_function(pName)) {
        auto func = GetWSIManager().get_function_pointer(pName);
        if (func) {
            return func;
        }
    }

    auto mali_proc_addr = LibraryLoader::Instance().GetMaliGetInstanceProcAddr();
    if (mali_proc_addr) {
        VkInstance mali_instance = instance;
        if (!mali_instance) {
            std::lock_guard<std::mutex> lock(instance_mutex);
            mali_instance = !managed_instances.empty() ? managed_instances.begin()->first : VK_NULL_HANDLE;
        }

        auto func = mali_proc_addr(mali_instance, pName);
        if (func) {
            return func;
        }
    }

    return nullptr;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL internal_vkGetDeviceProcAddr(VkDevice device, const char* pName) {
    using namespace mali_wrapper;

    if (!pName) {
        return nullptr;
    }

    uintptr_t device_ptr = reinterpret_cast<uintptr_t>(device);
    char hex_buffer[32];
    snprintf(hex_buffer, sizeof(hex_buffer), "0x%lx", device_ptr);

    if (strcmp(pName, "vkDestroyDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(internal_vkDestroyDevice);
    }

    if (GetWSIManager().is_wsi_function(pName)) {
        auto func = GetWSIManager().get_function_pointer(pName);
        if (func) {
            return func;
        } else {
            return nullptr;
        }
    }

    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(internal_vkGetDeviceProcAddr);
    }

    if (strstr(pName, "RayTracing") || strstr(pName, "MeshTask")) {
        return nullptr;
    }

    auto mali_proc_addr = LibraryLoader::Instance().GetMaliGetInstanceProcAddr();
    if (mali_proc_addr) {
        VkInstance parent_instance = get_device_parent_instance(device);
        if (parent_instance != VK_NULL_HANDLE) {
            auto mali_get_device_proc_addr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
                mali_proc_addr(parent_instance, "vkGetDeviceProcAddr"));
            if (mali_get_device_proc_addr) {
                auto func = mali_get_device_proc_addr(device, pName);
                if (func) {
                    return func;
                }
            }
        }
    }

    return nullptr;
}

static VKAPI_ATTR VkResult VKAPI_CALL wrapper_vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pSwapchainCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain) {
    using namespace mali_wrapper;

    uintptr_t device_ptr = reinterpret_cast<uintptr_t>(device);
    char hex_buffer[32];
    snprintf(hex_buffer, sizeof(hex_buffer), "0x%lx", device_ptr);


    void* device_key = nullptr;
    if (device != VK_NULL_HANDLE) {
        device_key = *reinterpret_cast<void**>(device);
        char key_hex[32];
        snprintf(key_hex, sizeof(key_hex), "0x%lx", reinterpret_cast<uintptr_t>(device_key));
    }

    void* wsi_lib = LibraryLoader::Instance().GetWSILibraryHandle();
    if (!wsi_lib) {
        LOG_ERROR("WSI layer library not available");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    auto wsi_create_swapchain = reinterpret_cast<PFN_vkCreateSwapchainKHR>(
        dlsym(wsi_lib, "wsi_layer_vkCreateSwapchainKHR"));
    if (!wsi_create_swapchain) {
        LOG_ERROR("WSI layer vkCreateSwapchainKHR function not found");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = wsi_create_swapchain(device, pSwapchainCreateInfo, pAllocator, pSwapchain);

    return result;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL filtered_mali_get_device_proc_addr(VkDevice device, const char* pName) {
    using namespace mali_wrapper;

    if (!pName) {
        return nullptr;
    }

    if (IsWSIFunction(pName)) {
        return nullptr;
    }

    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(internal_vkGetDeviceProcAddr);
    }

    auto mali_proc_addr = LibraryLoader::Instance().GetMaliGetInstanceProcAddr();
    if (mali_proc_addr) {
        VkInstance parent_instance = get_device_parent_instance(device);
        if (parent_instance != VK_NULL_HANDLE) {
            auto mali_get_device_proc_addr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
                mali_proc_addr(parent_instance, "vkGetDeviceProcAddr"));
            if (mali_get_device_proc_addr) {
                auto func = mali_get_device_proc_addr(device, pName);
                if (func) {
                    return func;
                } else {
                    return nullptr;
                }
            } else {
                return nullptr;
            }
        } else {
            return nullptr;
        }
    } else {
        return nullptr;
    }

    return nullptr;
}

static VKAPI_ATTR VkResult VKAPI_CALL dummy_set_device_loader_data(VkDevice device, void* object) {
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL internal_vkCreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice) {

    using namespace mali_wrapper;


    if (!pCreateInfo || !pDevice) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<const char *> enabled_extensions;
    std::unique_ptr<util::extension_list> extension_list_ptr;

    try
    {
        auto &instance_data = instance_private_data::get(physicalDevice);
        util::allocator extension_allocator(instance_data.get_allocator(), VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
        extension_list_ptr = std::make_unique<util::extension_list>(extension_allocator);
        auto &extensions = *extension_list_ptr;

        if (pCreateInfo->enabledExtensionCount > 0 && pCreateInfo->ppEnabledExtensionNames != nullptr)
        {
            extensions.add(pCreateInfo->ppEnabledExtensionNames, pCreateInfo->enabledExtensionCount);
        }

        VkResult extension_result = wsi::add_device_extensions_required_by_layer(
            physicalDevice, instance_data.get_enabled_platforms(), extensions);
        if (extension_result != VK_SUCCESS)
        {
            LOG_ERROR("Failed to collect WSI-required device extensions, error: " +
                      std::to_string(extension_result));
            return extension_result;
        }

        util::vector<const char *> extension_vector(extension_allocator);
        extensions.get_extension_strings(extension_vector);

        std::unordered_set<std::string> seen_extensions;
        seen_extensions.reserve(extension_vector.size());

        for (const char *name : extension_vector)
        {
            if (name == nullptr)
            {
                continue;
            }
            auto inserted = seen_extensions.emplace(name);
            if (inserted.second)
            {
                enabled_extensions.push_back(name);
            }
        }
    }
    catch (const std::exception &e)
    {
        LOG_WARN(std::string("Unable to augment device extensions: ") + e.what());
        enabled_extensions.clear();
        extension_list_ptr.reset();
    }

    const char *const *extension_name_ptr = pCreateInfo->ppEnabledExtensionNames;
    size_t extension_name_count = pCreateInfo->enabledExtensionCount;

    if (!enabled_extensions.empty())
    {
        extension_name_ptr = enabled_extensions.data();
        extension_name_count = enabled_extensions.size();
    }

    VkDeviceCreateInfo modified_create_info = *pCreateInfo;
    modified_create_info.enabledExtensionCount = static_cast<uint32_t>(extension_name_count);
    modified_create_info.ppEnabledExtensionNames = extension_name_ptr;

    auto mali_proc_addr = LibraryLoader::Instance().GetMaliGetInstanceProcAddr();
    if (!mali_proc_addr) {
        LOG_ERROR("Mali driver not available for device creation");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (managed_instances.empty()) {
        LOG_ERROR("No managed instance available for Mali device creation");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    auto &wsinst = instance_private_data::get(physicalDevice);
    VkInstance mali_instance = wsinst.get_instance_handle();

    auto mali_create_device = reinterpret_cast<PFN_vkCreateDevice>(
        mali_proc_addr(mali_instance, "vkCreateDevice"));

    if (!mali_create_device) {
        LOG_ERROR("Mali driver vkCreateDevice not available");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = mali_create_device(physicalDevice, &modified_create_info, pAllocator, pDevice);

    if (result == VK_SUCCESS) {
        LOG_INFO("Device created successfully through Mali driver");

        managed_devices.emplace(*pDevice, mali_instance);

        VkInstance target_mali_instance = mali_instance;
        {
            std::lock_guard<std::mutex> lock(instance_mutex);
            if (latest_instance != VK_NULL_HANDLE) {
                auto latest_it = managed_instances.find(latest_instance);
                if (latest_it != managed_instances.end()) {
                    target_mali_instance = latest_instance;
                }
            }

            if (target_mali_instance == mali_instance && !managed_instances.empty()) {
                auto fallback = managed_instances.begin()->second.get();
                if (fallback != nullptr) {
                    target_mali_instance = fallback->instance;
                }
            }

            if (target_mali_instance != mali_instance) {
            } else if (!managed_instances.empty()) {
            } else {
            }
        }

        VkResult wsi_result = GetWSIManager().init_device(target_mali_instance, physicalDevice, *pDevice,
                                                         extension_name_ptr, extension_name_count);
        if (wsi_result != VK_SUCCESS) {
            LOG_ERROR("Failed to initialize WSI manager for device, error: " + std::to_string(wsi_result));
        } else {
            LOG_INFO("WSI manager initialized for device: " + std::to_string(reinterpret_cast<uintptr_t>(*pDevice)));
        }
    } else {
        LOG_ERROR("Failed to create device through Mali driver, error: " + std::to_string(result));
    }

    return result;
}

static VKAPI_ATTR void VKAPI_CALL internal_vkDestroyDevice(
    VkDevice device,
    const VkAllocationCallbacks* pAllocator) {

    using namespace mali_wrapper;


    if (device == VK_NULL_HANDLE) {
        return;
    }

    VkInstance parent_instance = get_device_parent_instance(device);

    if (managed_devices.find(device) == managed_devices.end()) {
        LOG_WARN("Destroying unmanaged device");
    }

    GetWSIManager().release_device(device);

    auto mali_proc_addr = LibraryLoader::Instance().GetMaliGetInstanceProcAddr();
    PFN_vkDestroyDevice mali_destroy = nullptr;

    if (mali_proc_addr && parent_instance != VK_NULL_HANDLE) {
        auto mali_get_device_proc_addr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            mali_proc_addr(parent_instance, "vkGetDeviceProcAddr"));
        if (mali_get_device_proc_addr) {
            mali_destroy = reinterpret_cast<PFN_vkDestroyDevice>(
                mali_get_device_proc_addr(device, "vkDestroyDevice"));
        }
    }

    if (!mali_destroy) {
        mali_destroy = reinterpret_cast<PFN_vkDestroyDevice>(
            LibraryLoader::Instance().GetMaliProcAddr("vkDestroyDevice"));
    }

    if (mali_destroy) {
        mali_destroy(device, pAllocator);
        LOG_INFO("Device destroyed successfully");
    } else {
        LOG_WARN("Failed to locate Mali vkDestroyDevice entry point");
    }

    managed_devices.erase(device);
}

static VKAPI_ATTR VkResult VKAPI_CALL mali_driver_create_device(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice) {

    using namespace mali_wrapper;


    if (!pCreateInfo || !pDevice) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    auto mali_proc_addr = LibraryLoader::Instance().GetMaliGetInstanceProcAddr();
    if (!mali_proc_addr) {
        LOG_ERROR("Mali driver not available for device creation");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    auto mali_create_instance = LibraryLoader::Instance().GetMaliCreateInstance();
    if (!mali_create_instance) {
        LOG_ERROR("Mali driver vkCreateInstance not available");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkInstance mali_instance = VK_NULL_HANDLE;
    {
        std::lock_guard<std::mutex> lock(instance_mutex);
        if (managed_instances.empty()) {
            LOG_ERROR("No managed instance available for Mali device creation");
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        if (managed_instances.size() >= 2) {
            auto it = std::prev(managed_instances.end());
            mali_instance = it->first;
        } else {
            mali_instance = managed_instances.begin()->first;
        }
    }

    auto mali_create_device = reinterpret_cast<PFN_vkCreateDevice>(
        mali_proc_addr(mali_instance, "vkCreateDevice"));

    if (!mali_create_device) {
        mali_create_device = reinterpret_cast<PFN_vkCreateDevice>(
            LibraryLoader::Instance().GetMaliProcAddr("vkCreateDevice"));
    }

    if (!mali_create_device) {
        LOG_ERROR("Mali driver vkCreateDevice not available through any method");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = mali_create_device(physicalDevice, pCreateInfo, pAllocator, pDevice);

    if (result == VK_SUCCESS) {
        VkInstance target_mali_instance = mali_instance;
        {
            std::lock_guard<std::mutex> lock(instance_mutex);
            if (managed_instances.size() >= 2) {
                auto it = std::prev(managed_instances.end());
                target_mali_instance = it->first;  // This is the Mali instance, not application instance
            } else if (!managed_instances.empty()) {
                target_mali_instance = managed_instances.begin()->first;
            }
        }

        managed_devices.emplace(*pDevice, mali_instance);

        VkResult wsi_result = GetWSIManager().init_device(target_mali_instance, physicalDevice, *pDevice,
                                                         pCreateInfo->ppEnabledExtensionNames, pCreateInfo->enabledExtensionCount);
        if (wsi_result != VK_SUCCESS) {
            LOG_ERROR("Failed to initialize WSI manager for device, error: " + std::to_string(wsi_result));
        } else {
        }
    } else {
        LOG_ERROR("Mali driver device creation failed, error: " + std::to_string(result));
    }

    return result;
}

extern "C" {

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName) {
    using namespace mali_wrapper;

    if (!pName) {
        return nullptr;
    }

    static bool initialized = false;
    if (!initialized) {
        if (!InitializeWrapper()) {
            return nullptr;
        }
        initialized = true;
    }

    return internal_vkGetInstanceProcAddr(instance, pName);
}

VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion) {
    if (pSupportedVersion) {
        *pSupportedVersion = 5;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    return vk_icdGetInstanceProcAddr(instance, pName);
}

} // extern "C"
