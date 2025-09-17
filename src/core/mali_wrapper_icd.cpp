#include "mali_wrapper_icd.hpp"
#include "library_loader.hpp"
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

namespace mali_wrapper {

// Static instance tracker
static std::unordered_set<VkInstance> managed_instances;

// Device handle mapping for WSI layer compatibility
static std::unordered_map<VkDevice, VkDevice> device_handle_map;

// Preserve original WSI device handles to maintain their internal structure
static std::unordered_map<VkDevice, VkDevice> wsi_device_preserve_map;

// WSI functions that should be routed to the WSI layer
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
    // "vkGetSwapchainCounterEXT",
    "vkReleaseSwapchainImagesEXT",
    // "vkWaitForPresentKHR",
    // "vkSetHdrMetadataEXT",
    // "vkSetLocalDimmingAMD",
    // "vkGetPastPresentationTimingGOOGLE",
    // "vkGetRefreshCycleDurationGOOGLE",
    // "vkAcquireFullScreenExclusiveModeEXT",
    // "vkReleaseFullScreenExclusiveModeEXT",

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

// Find the canonical device handle for WSI layer operations
static VkDevice GetCanonicalDevice(VkDevice device) {
    auto it = device_handle_map.find(device);
    if (it != device_handle_map.end()) {
        return it->second;
    }

    // If not found, check if this device is similar to any registered device
    // This handles cases where the application might be using wrapped device handles
    uintptr_t device_ptr = reinterpret_cast<uintptr_t>(device);
    for (const auto& pair : device_handle_map) {
        uintptr_t registered_ptr = reinterpret_cast<uintptr_t>(pair.first);

        // Check if devices have similar address patterns (same lower 32 bits)
        if ((device_ptr & 0xFFFFFFFF) == (registered_ptr & 0xFFFFFFFF)) {
            char hex1[32], hex2[32];
            snprintf(hex1, sizeof(hex1), "0x%lx", device_ptr);
            snprintf(hex2, sizeof(hex2), "0x%lx", registered_ptr);

            LOG_ERROR("DEVICE_MAP: Mapping device " + std::string(hex1) + " to canonical " + std::string(hex2));
            device_handle_map[device] = pair.second; // Cache this mapping
            return pair.second;
        }
    }

    // If no mapping found, return original device
    return device;
}


bool InitializeWrapper() {
    // Enable debug logging if environment variable is set
    if (getenv("MALI_WRAPPER_DEBUG")) {
        Logger::Instance().SetLevel(LogLevel::DEBUG);
        LOG_DEBUG("Debug logging enabled via MALI_WRAPPER_DEBUG");
    }

    LOG_INFO("Initializing Mali Wrapper ICD");

    // Load libraries with build-time configuration
    if (!LibraryLoader::Instance().LoadLibraries()) {
        LOG_ERROR("Failed to load required libraries");
        return false;
    }

    LOG_INFO("Mali Wrapper ICD initialized successfully");
    return true;
}

void ShutdownWrapper() {
    LOG_INFO("Shutting down Mali Wrapper ICD");
    LibraryLoader::Instance().UnloadLibraries();
}

} // namespace mali_wrapper

// Dummy loader callback for WSI layer
static VKAPI_ATTR VkResult VKAPI_CALL dummy_set_instance_loader_data(VkInstance instance, void* object) {
    // The WSI layer expects this callback but we don't need it for our use case
    return VK_SUCCESS;
}

// Forward declarations
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL internal_vkGetDeviceProcAddr(VkDevice device, const char* pName);
static VKAPI_ATTR VkResult VKAPI_CALL internal_vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice);
static VKAPI_ATTR VkResult VKAPI_CALL mali_driver_create_device(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice);

// Wrapper for vkCreateSwapchainKHR that handles device mapping
static VKAPI_ATTR VkResult VKAPI_CALL wrapper_vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pSwapchainCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain);

// Filtered Mali GetInstanceProcAddr that returns nullptr for WSI functions
// This tells the WSI layer that Mali driver doesn't support WSI functions
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL filtered_mali_get_instance_proc_addr(VkInstance instance, const char* pName) {
    using namespace mali_wrapper;

    if (!pName) {
        return nullptr;
    }

    LOG_DEBUG("filtered_mali_get_instance_proc_addr called for: " + std::string(pName));

    // Return nullptr for WSI functions so WSI layer knows Mali driver doesn't support them
    if (IsWSIFunction(pName)) {
        LOG_DEBUG("Filtered out WSI function from Mali driver: " + std::string(pName));
        return nullptr;
    }

    // Special case: vkCreateDevice needs to call Mali driver but isn't available through GetInstanceProcAddr
    if (strcmp(pName, "vkCreateDevice") == 0) {
        LOG_DEBUG("Providing Mali driver vkCreateDevice wrapper");
        return reinterpret_cast<PFN_vkVoidFunction>(mali_driver_create_device);
    }

    // Forward non-WSI functions to Mali driver
    auto mali_proc_addr = LibraryLoader::Instance().GetMaliGetInstanceProcAddr();
    if (mali_proc_addr) {
        auto func = mali_proc_addr(instance, pName);
        if (func) {
            LOG_DEBUG("Forwarded non-WSI function to Mali driver: " + std::string(pName) + " -> SUCCESS");
        } else {
            LOG_DEBUG("Forwarded non-WSI function to Mali driver: " + std::string(pName) + " -> FAILED (not available)");
        }
        return func;
    }

    LOG_DEBUG("Mali driver GetInstanceProcAddr not available");
    return nullptr;
}

// Internal function implementations
static VKAPI_ATTR VkResult VKAPI_CALL internal_vkCreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance) {

    using namespace mali_wrapper;

    LOG_INFO("internal_vkCreateInstance called");

    if (!pCreateInfo || !pInstance) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Set up layer chain: WSI layer -> Mali driver
    LOG_DEBUG("Setting up layer chain for WSI layer -> Mali driver");

    // Create layer chain info structures
    VkLayerInstanceCreateInfo layer_link_info = {};
    layer_link_info.sType = VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO;
    layer_link_info.function = VK_LAYER_LINK_INFO;
    layer_link_info.u.pLayerInfo = nullptr; // We'll set this below
    layer_link_info.pNext = const_cast<void*>(pCreateInfo->pNext);

    VkLayerInstanceCreateInfo loader_callback_info = {};
    loader_callback_info.sType = VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO;
    loader_callback_info.function = VK_LOADER_DATA_CALLBACK;
    loader_callback_info.u.pfnSetInstanceLoaderData = dummy_set_instance_loader_data;
    loader_callback_info.pNext = &layer_link_info;

    // Create layer info pointing to Mali driver (filtered to exclude WSI functions)
    VkLayerInstanceLink mali_layer_info = {};
    mali_layer_info.pfnNextGetInstanceProcAddr = filtered_mali_get_instance_proc_addr;
    mali_layer_info.pNext = nullptr;

    layer_link_info.u.pLayerInfo = &mali_layer_info;

    // Create modified create info with layer chain
    VkInstanceCreateInfo modified_create_info = *pCreateInfo;
    modified_create_info.pNext = &loader_callback_info;

    // Get WSI layer's create instance function
    auto wsi_proc_addr = LibraryLoader::Instance().GetWSIGetInstanceProcAddr();
    if (!wsi_proc_addr) {
        LOG_ERROR("WSI layer not available for instance creation");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    auto wsi_create_instance = reinterpret_cast<PFN_vkCreateInstance>(
        wsi_proc_addr(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!wsi_create_instance) {
        LOG_ERROR("WSI layer vkCreateInstance not available");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    LOG_DEBUG("Creating instance through WSI layer with proper layer chain");
    VkResult result = wsi_create_instance(&modified_create_info, pAllocator, pInstance);

    if (result == VK_SUCCESS) {
        managed_instances.insert(*pInstance);
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

    LOG_INFO("internal_vkDestroyInstance called");

    if (instance == VK_NULL_HANDLE) {
        return;
    }

    // Check if this is our managed instance
    auto it = managed_instances.find(instance);
    if (it == managed_instances.end()) {
        LOG_WARN("Destroying unmanaged instance");
        return;
    }

    // Destroy Mali driver instance
    auto mali_proc_addr = LibraryLoader::Instance().GetMaliGetInstanceProcAddr();
    if (mali_proc_addr) {
        auto mali_destroy = reinterpret_cast<PFN_vkDestroyInstance>(
            mali_proc_addr(instance, "vkDestroyInstance"));
        if (mali_destroy) {
            LOG_DEBUG("Calling Mali vkDestroyInstance");
            mali_destroy(instance, pAllocator);
        } else {
            LOG_DEBUG("Mali vkDestroyInstance not available - instance cleanup handled by wrapper");
        }
    }

    // Remove from our managed instances
    managed_instances.erase(it);
    LOG_INFO("Instance destroyed successfully");
}

static VKAPI_ATTR VkResult VKAPI_CALL internal_vkEnumerateInstanceExtensionProperties(
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties) {

    using namespace mali_wrapper;

    LOG_DEBUG("internal_vkEnumerateInstanceExtensionProperties called");

    if (pLayerName != nullptr) {
        // We don't support layer-specific extensions
        *pPropertyCount = 0;
        return VK_SUCCESS;
    }

    // Get Mali driver extensions
    std::vector<VkExtensionProperties> mali_extensions;
    auto mali_proc_addr = LibraryLoader::Instance().GetMaliGetInstanceProcAddr();
    if (mali_proc_addr) {
        auto mali_enumerate = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
            mali_proc_addr(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties"));
        if (mali_enumerate) {
            uint32_t mali_count = 0;
            VkResult result = mali_enumerate(nullptr, &mali_count, nullptr);
            if (result == VK_SUCCESS && mali_count > 0) {
                mali_extensions.resize(mali_count);
                mali_enumerate(nullptr, &mali_count, mali_extensions.data());
                LOG_DEBUG("Mali driver provides " + std::to_string(mali_count) + " extensions");
            }
        }
    }

    // Add WSI extensions that we know the layer supports
    std::vector<VkExtensionProperties> wsi_extensions;
    if (LibraryLoader::Instance().GetWSIGetInstanceProcAddr()) {
        // Common WSI extensions supported by the layer
        const char* wsi_extension_names[] = {
            "VK_KHR_surface",
            "VK_KHR_wayland_surface",
            "VK_KHR_xcb_surface",
            "VK_KHR_xlib_surface",
            "VK_KHR_get_surface_capabilities2",
            "VK_EXT_surface_maintenance1"
        };

        for (const char* ext_name : wsi_extension_names) {
            VkExtensionProperties ext = {};
            strncpy(ext.extensionName, ext_name, VK_MAX_EXTENSION_NAME_SIZE - 1);
            ext.specVersion = 1;  // Default spec version
            wsi_extensions.push_back(ext);
        }

        LOG_DEBUG("Added " + std::to_string(wsi_extensions.size()) + " WSI extensions");
    }

    // Combine extensions, avoiding duplicates
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

    LOG_DEBUG("Combined total: " + std::to_string(combined_extensions.size()) + " extensions");

    // Return results
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

    LOG_DEBUG("internal_vkGetInstanceProcAddr called for: " + std::string(pName));

    // Handle our wrapper functions first
    if (strcmp(pName, "vkGetInstanceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(internal_vkGetInstanceProcAddr);
    }

    if (strcmp(pName, "vkCreateInstance") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(internal_vkCreateInstance);
    }

    if (strcmp(pName, "vkDestroyInstance") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(internal_vkDestroyInstance);
    }

    // Handle extension enumeration specially - combine Mali + WSI extensions
    if (strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(internal_vkEnumerateInstanceExtensionProperties);
    }

    // Handle vkGetDeviceProcAddr specially - return our wrapper to route device WSI functions
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(internal_vkGetDeviceProcAddr);
    }

    // Handle vkCreateDevice - use our wrapper to set up layer chain properly
    if (strcmp(pName, "vkCreateDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(internal_vkCreateDevice);
    }

    // Route WSI functions to WSI layer
    if (IsWSIFunction(pName)) {
        LOG_DEBUG("Attempting to route WSI function to WSI layer: " + std::string(pName));
        auto wsi_proc_addr = LibraryLoader::Instance().GetWSIGetInstanceProcAddr();
        if (wsi_proc_addr) {
            try {
                // Pass the actual instance to WSI layer - it created this instance so it knows about it
                auto func = wsi_proc_addr(instance, pName);
                if (func) {
                    LOG_DEBUG("WSI function successfully routed to WSI layer: " + std::string(pName));
                    return func;
                } else {
                    LOG_DEBUG("WSI function returned null from WSI layer: " + std::string(pName));
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Exception calling WSI layer for " + std::string(pName) + ": " + e.what());
            } catch (...) {
                LOG_ERROR("Unknown exception calling WSI layer for " + std::string(pName));
            }
        } else {
            LOG_DEBUG("WSI layer GetInstanceProcAddr not available for: " + std::string(pName));
        }
    }

    // Forward all other functions to Mali driver
    auto mali_proc_addr = LibraryLoader::Instance().GetMaliGetInstanceProcAddr();
    if (mali_proc_addr) {
        VkInstance mali_instance = instance ? instance :
            (!managed_instances.empty() ? *managed_instances.begin() : VK_NULL_HANDLE);

        auto func = mali_proc_addr(mali_instance, pName);
        if (func) {
            LOG_DEBUG("Function routed to Mali driver: " + std::string(pName));
            return func;
        }
    }

    LOG_DEBUG("Function not available: " + std::string(pName));
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

    LOG_DEBUG("internal_vkGetDeviceProcAddr called for: " + std::string(pName) + " with device: " + std::to_string(device_ptr));

    // Special handling for vkCreateSwapchainKHR - use our wrapper for device mapping
    if (strcmp(pName, "vkCreateSwapchainKHR") == 0) {
        LOG_ERROR("=== RETURNING SWAPCHAIN WRAPPER ===");
        LOG_ERROR("PROCADDR: Returning wrapper for vkCreateSwapchainKHR");
        return reinterpret_cast<PFN_vkVoidFunction>(wrapper_vkCreateSwapchainKHR);
    }

    // For other WSI device functions, get them directly from WSI layer using dlsym to avoid device tracking issues
    if (IsWSIFunction(pName)) {
        LOG_ERROR("=== WSI FUNCTION REQUEST TRACKING ===");
        LOG_ERROR("WSI_REQUEST: Function " + std::string(pName) + " requested for device decimal = " + std::to_string(device_ptr));
        LOG_ERROR("WSI_REQUEST: Function " + std::string(pName) + " requested for device hex = " + std::string(hex_buffer));

        // Try to map to canonical device handle for WSI layer compatibility
        VkDevice canonical_device = GetCanonicalDevice(device);
        if (canonical_device != device) {
            uintptr_t canonical_ptr = reinterpret_cast<uintptr_t>(canonical_device);
            char canonical_hex[32];
            snprintf(canonical_hex, sizeof(canonical_hex), "0x%lx", canonical_ptr);
            LOG_ERROR("WSI_REQUEST: Mapped to canonical device " + std::string(canonical_hex));

            // Use the canonical device for WSI layer calls
            device = canonical_device;
            LOG_ERROR("WSI_REQUEST: Using canonical device for WSI layer call");
        }
        LOG_ERROR("=== CHECKING IF THIS MATCHES CREATED DEVICE ===");

        // If no mapping found, try to register this device with the WSI layer dynamically
        if (canonical_device == device && device_handle_map.empty()) {
            LOG_ERROR("WSI_REQUEST: No device mapping found, attempting to register device with WSI layer");

            // Try to call WSI layer's device creation/registration function
            void* wsi_lib = LibraryLoader::Instance().GetWSILibraryHandle();
            if (wsi_lib) {
                // Attempt to get the WSI layer's device creation function and call it
                void* wsi_create_device_func = dlsym(wsi_lib, "wsi_layer_vkCreateDevice");
                if (wsi_create_device_func) {
                    LOG_ERROR("WSI_REQUEST: Found WSI layer device creation function, device may need re-registration");
                    // The device should already be created through the layer chain, this might just be a tracking issue

                    // Register the device mapping as itself for now
                    device_handle_map[device] = device;
                    LOG_ERROR("WSI_REQUEST: Registered device " + std::string(hex_buffer) + " with itself as canonical");
                } else {
                    LOG_ERROR("WSI_REQUEST: WSI layer device creation function not found");
                }
            }
        }

        // Get WSI layer function directly using dlsym to bypass device tracking
        void* wsi_lib = LibraryLoader::Instance().GetWSILibraryHandle();
        if (wsi_lib) {
            std::string wsi_func_name = "wsi_layer_" + std::string(pName);
            void* func = dlsym(wsi_lib, wsi_func_name.c_str());
            if (func) {
                LOG_ERROR("SUCCESS: WSI device function obtained via dlsym: " + std::string(pName) + " -> " + wsi_func_name);
                return reinterpret_cast<PFN_vkVoidFunction>(func);
            } else {
                LOG_DEBUG("WSI function not found via dlsym: " + wsi_func_name + " error: " + (dlerror() ? dlerror() : "unknown"));
            }
        }

        // For missing WSI functions, return a stub that returns VK_ERROR_FEATURE_NOT_PRESENT
        LOG_DEBUG("WSI function not available in layer, returning error stub: " + std::string(pName));

        // Create a simple stub function that returns VK_ERROR_FEATURE_NOT_PRESENT
        static const auto stub_function = +[]() -> VkResult {
            return VK_ERROR_FEATURE_NOT_PRESENT;
        };
        return reinterpret_cast<PFN_vkVoidFunction>(stub_function);
    }

    // Special case: Never forward vkGetDeviceProcAddr - always return our wrapper
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        LOG_ERROR("CRITICAL: Returning our wrapper for vkGetDeviceProcAddr instead of Mali driver");
        return reinterpret_cast<PFN_vkVoidFunction>(internal_vkGetDeviceProcAddr);
    }

    // Skip problematic extension queries that may corrupt Mali driver state
    if (strstr(pName, "RayTracing") || strstr(pName, "MeshTask")) {
        LOG_DEBUG("Skipping problematic extension function to prevent Mali driver corruption: " + std::string(pName));
        return nullptr;
    }

    // Forward all other device functions to Mali driver
    LOG_DEBUG("Forwarding non-WSI device function to Mali driver: " + std::string(pName));
    auto mali_proc_addr = LibraryLoader::Instance().GetMaliGetInstanceProcAddr();
    if (mali_proc_addr && !managed_instances.empty()) {
        auto mali_get_device_proc_addr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            mali_proc_addr(*managed_instances.begin(), "vkGetDeviceProcAddr"));
        if (mali_get_device_proc_addr) {
            auto func = mali_get_device_proc_addr(device, pName);
            if (func) {
                LOG_DEBUG("Device function routed to Mali driver: " + std::string(pName));
                return func;
            }
        }
    }

    LOG_DEBUG("Device function not available: " + std::string(pName));
    return nullptr;
}

// Wrapper for vkCreateSwapchainKHR that handles device mapping
static VKAPI_ATTR VkResult VKAPI_CALL wrapper_vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pSwapchainCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain) {
    using namespace mali_wrapper;

    uintptr_t device_ptr = reinterpret_cast<uintptr_t>(device);
    char hex_buffer[32];
    snprintf(hex_buffer, sizeof(hex_buffer), "0x%lx", device_ptr);

    LOG_ERROR("=== WRAPPER SWAPCHAIN CREATION ===");
    LOG_ERROR("WRAPPER: vkCreateSwapchainKHR called with device decimal = " + std::to_string(device_ptr));
    LOG_ERROR("WRAPPER: vkCreateSwapchainKHR called with device hex = " + std::string(hex_buffer));

    // Check what the device handle points to (WSI layer uses this as key)
    void* device_key = nullptr;
    if (device != VK_NULL_HANDLE) {
        device_key = *reinterpret_cast<void**>(device);
        char key_hex[32];
        snprintf(key_hex, sizeof(key_hex), "0x%lx", reinterpret_cast<uintptr_t>(device_key));
        LOG_ERROR("WRAPPER: Device handle points to (WSI key) = " + std::string(key_hex));
    }

    // BYPASS APPROACH: Since device handle gets corrupted, we need to find the WSI device in the layer's tracking
    // The WSI layer has the device registered, we just need to find the device that has the right key

    LOG_ERROR("WRAPPER: Attempting WSI layer device lookup bypass");

    // For now, try the direct approach - the WSI layer should find the device based on some internal lookup
    // The WSI layer might be able to handle the lookup differently than expected

    LOG_ERROR("WRAPPER: Using original device handle for WSI layer call (trusting WSI layer internal handling)");

    // Get the actual WSI layer function
    void* wsi_lib = LibraryLoader::Instance().GetWSILibraryHandle();
    if (!wsi_lib) {
        LOG_ERROR("WRAPPER: WSI layer library not available");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    auto wsi_create_swapchain = reinterpret_cast<PFN_vkCreateSwapchainKHR>(
        dlsym(wsi_lib, "wsi_layer_vkCreateSwapchainKHR"));
    if (!wsi_create_swapchain) {
        LOG_ERROR("WRAPPER: WSI layer vkCreateSwapchainKHR function not found");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    LOG_ERROR("WRAPPER: Calling WSI layer vkCreateSwapchainKHR with mapped device");
    VkResult result = wsi_create_swapchain(device, pSwapchainCreateInfo, pAllocator, pSwapchain);
    LOG_ERROR("WRAPPER: WSI layer vkCreateSwapchainKHR returned: " + std::to_string(result));

    return result;
}

// Filtered Mali GetDeviceProcAddr that routes WSI functions properly
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL filtered_mali_get_device_proc_addr(VkDevice device, const char* pName) {
    using namespace mali_wrapper;

    if (!pName) {
        return nullptr;
    }

    // Return nullptr for WSI device functions so WSI layer handles them
    if (IsWSIFunction(pName)) {
        LOG_DEBUG("Filtered out WSI device function from Mali driver: " + std::string(pName));
        return nullptr;
    }

    // Never return Mali's vkGetDeviceProcAddr - return our wrapper instead
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        LOG_ERROR("CRITICAL FIX: WSI layer requested vkGetDeviceProcAddr - returning our wrapper");
        return reinterpret_cast<PFN_vkVoidFunction>(internal_vkGetDeviceProcAddr);
    }

    // Forward non-WSI device functions to Mali driver
    auto mali_proc_addr = LibraryLoader::Instance().GetMaliGetInstanceProcAddr();
    if (mali_proc_addr && !managed_instances.empty()) {
        auto mali_get_device_proc_addr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            mali_proc_addr(*managed_instances.begin(), "vkGetDeviceProcAddr"));
        if (mali_get_device_proc_addr) {
            auto func = mali_get_device_proc_addr(device, pName);
            if (func) {
                LOG_DEBUG("Forwarded non-WSI device function to Mali driver: " + std::string(pName));
                return func;
            } else {
                LOG_DEBUG("Mali driver returned null for non-WSI function: " + std::string(pName));
                return nullptr;
            }
        } else {
            LOG_DEBUG("Mali driver GetDeviceProcAddr not available");
            return nullptr;
        }
    } else {
        LOG_DEBUG("Mali proc addr or managed instances not available");
        return nullptr;
    }

    return nullptr;
}

// Dummy device loader callback
static VKAPI_ATTR VkResult VKAPI_CALL dummy_set_device_loader_data(VkDevice device, void* object) {
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL internal_vkCreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice) {

    using namespace mali_wrapper;

    LOG_INFO("internal_vkCreateDevice called");

    if (!pCreateInfo || !pDevice) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Call WSI layer's exported vkCreateDevice directly to ensure proper device tracking
    void* wsi_lib = LibraryLoader::Instance().GetWSILibraryHandle();
    if (!wsi_lib) {
        LOG_ERROR("WSI layer library not available for device creation");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Get the exported wsi_layer_vkCreateDevice function via dlsym
    typedef VkResult (*PFN_wsi_layer_vkCreateDevice)(VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*, VkDevice*);
    auto wsi_create_device = reinterpret_cast<PFN_wsi_layer_vkCreateDevice>(
        dlsym(wsi_lib, "wsi_layer_vkCreateDevice"));

    if (!wsi_create_device) {
        LOG_ERROR("wsi_layer_vkCreateDevice not found via dlsym: " + std::string(dlerror() ? dlerror() : "unknown"));
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Proper layer chain: WSI layer -> Mali driver (with filtering)
    VkLayerDeviceCreateInfo device_layer_link_info = {};
    device_layer_link_info.sType = VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO;
    device_layer_link_info.function = VK_LAYER_LINK_INFO;
    device_layer_link_info.pNext = const_cast<void*>(pCreateInfo->pNext);

    VkLayerDeviceCreateInfo device_loader_callback_info = {};
    device_loader_callback_info.sType = VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO;
    device_loader_callback_info.function = VK_LOADER_DATA_CALLBACK;
    device_loader_callback_info.u.pfnSetDeviceLoaderData = dummy_set_device_loader_data;
    device_loader_callback_info.pNext = &device_layer_link_info;

    VkLayerDeviceLink mali_device_layer_info = {};
    mali_device_layer_info.pfnNextGetInstanceProcAddr = filtered_mali_get_instance_proc_addr;
    mali_device_layer_info.pfnNextGetDeviceProcAddr = filtered_mali_get_device_proc_addr;
    mali_device_layer_info.pNext = nullptr;

    device_layer_link_info.u.pLayerInfo = &mali_device_layer_info;

    VkDeviceCreateInfo modified_create_info = *pCreateInfo;
    modified_create_info.pNext = &device_loader_callback_info;

    LOG_DEBUG("Creating device through WSI layer with proper filtered layer chain");
    VkResult result = wsi_create_device(physicalDevice, &modified_create_info, pAllocator, pDevice);

    if (result == VK_SUCCESS) {
        LOG_INFO("Device created successfully through WSI layer with proper tracking");

        // COMPREHENSIVE DEVICE HANDLE TRACKING
        uintptr_t device_ptr = reinterpret_cast<uintptr_t>(*pDevice);
        char hex_buffer[32];
        snprintf(hex_buffer, sizeof(hex_buffer), "0x%lx", device_ptr);

        LOG_ERROR("=== DEVICE CREATION TRACKING ===");
        LOG_ERROR("DEVICE_CREATE: WSI returned device decimal = " + std::to_string(device_ptr));
        LOG_ERROR("DEVICE_CREATE: WSI returned device hex = " + std::string(hex_buffer));

        // Register this device handle as the canonical one
        device_handle_map[*pDevice] = *pDevice;
        LOG_ERROR("DEVICE_MAP: Registered canonical device handle " + std::string(hex_buffer));

        // CRITICAL: The device handle structure will be modified by Mali driver
        // We need to create a separate WSI-only device handle by calling WSI layer directly
        // For now, store the device handle but we know it will be corrupted

        // Store the expected WSI key value for validation
        void* wsi_key = *reinterpret_cast<void**>(*pDevice);
        char wsi_key_hex[32];
        snprintf(wsi_key_hex, sizeof(wsi_key_hex), "0x%lx", reinterpret_cast<uintptr_t>(wsi_key));
        LOG_ERROR("DEVICE_PRESERVE: WSI device key at creation = " + std::string(wsi_key_hex));

        // Store the device for WSI operations (knowing it will be modified)
        wsi_device_preserve_map[*pDevice] = *pDevice;
        LOG_ERROR("DEVICE_PRESERVE: Device handle stored (will likely be corrupted by Mali driver)");
        LOG_ERROR("=== APP WILL RECEIVE THIS DEVICE ===");
    } else {
        LOG_ERROR("Failed to create device through WSI layer, error: " + std::to_string(result));
    }

    return result;
}

// Mali driver create device function for WSI layer chain
static VKAPI_ATTR VkResult VKAPI_CALL mali_driver_create_device(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice) {

    using namespace mali_wrapper;

    LOG_DEBUG("mali_driver_create_device called for WSI layer chain");

    if (!pCreateInfo || !pDevice) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Get Mali driver's create instance function to get the create device function
    auto mali_proc_addr = LibraryLoader::Instance().GetMaliGetInstanceProcAddr();
    if (!mali_proc_addr) {
        LOG_ERROR("Mali driver not available for device creation");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // We need to call the Mali driver's create device through its instance
    // Since Mali driver doesn't export vkCreateDevice through GetInstanceProcAddr,
    // we need to call the create instance first and then get the device creation
    auto mali_create_instance = LibraryLoader::Instance().GetMaliCreateInstance();
    if (!mali_create_instance) {
        LOG_ERROR("Mali driver vkCreateInstance not available");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Since we're being called from the WSI layer in a layer chain context,
    // we should have an instance already. We'll use our managed instance.
    if (managed_instances.empty()) {
        LOG_ERROR("No managed instance available for Mali device creation");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkInstance mali_instance = *managed_instances.begin();

    // Try to get vkCreateDevice from Mali driver through different approaches
    auto mali_create_device = reinterpret_cast<PFN_vkCreateDevice>(
        mali_proc_addr(mali_instance, "vkCreateDevice"));

    if (!mali_create_device) {
        // Try getting it as a global function from the Mali library
        mali_create_device = reinterpret_cast<PFN_vkCreateDevice>(
            LibraryLoader::Instance().GetMaliProcAddr("vkCreateDevice"));
    }

    if (!mali_create_device) {
        LOG_ERROR("Mali driver vkCreateDevice not available through any method");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    LOG_DEBUG("Calling Mali driver vkCreateDevice directly");
    VkResult result = mali_create_device(physicalDevice, pCreateInfo, pAllocator, pDevice);

    if (result == VK_SUCCESS) {
        LOG_DEBUG("Mali driver device created successfully");
    } else {
        LOG_ERROR("Mali driver device creation failed, error: " + std::to_string(result));
    }

    return result;
}

extern "C" {

// Main ICD entry point - pure passthrough to internal proc addr
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName) {
    using namespace mali_wrapper;

    if (!pName) {
        return nullptr;
    }

    // Initialize on first call
    static bool initialized = false;
    if (!initialized) {
        if (!InitializeWrapper()) {
            return nullptr;
        }
        initialized = true;
    }

    LOG_DEBUG("vk_icdGetInstanceProcAddr called for: " + std::string(pName));

    // Pure passthrough to internal implementation
    return internal_vkGetInstanceProcAddr(instance, pName);
}

// Required for ICD manifest - returns the negotiate interface version
VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion) {
    // We support loader interface version 5
    if (pSupportedVersion) {
        *pSupportedVersion = 5;
    }
    return VK_SUCCESS;
}

// Export alias for compatibility with direct loading (for our test)
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    return vk_icdGetInstanceProcAddr(instance, pName);
}

} // extern "C"