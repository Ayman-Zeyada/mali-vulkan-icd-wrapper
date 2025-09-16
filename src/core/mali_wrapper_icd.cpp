#include "mali_wrapper_icd.hpp"
#include "library_loader.hpp"
#include "config.hpp"
#include "../utils/logging.hpp"
#include <cstring>
#include <unordered_set>
#include <cstdlib>

namespace mali_wrapper {

// Static instance tracker
static std::unordered_set<VkInstance> managed_instances;

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

    // Forward directly to Mali driver without WSI layer
    auto mali_create_instance = LibraryLoader::Instance().GetMaliCreateInstance();
    if (!mali_create_instance) {
        LOG_ERROR("Mali vkCreateInstance not available");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    LOG_DEBUG("Creating instance with Mali driver only");
    VkResult result = mali_create_instance(pCreateInfo, pAllocator, pInstance);
    if (result == VK_SUCCESS) {
        managed_instances.insert(*pInstance);
        LOG_INFO("Instance created successfully with Mali driver");
    } else {
        LOG_ERROR("Failed to create instance, error: " + std::to_string(result));
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

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL internal_vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    using namespace mali_wrapper;

    if (!pName) {
        return nullptr;
    }

    LOG_DEBUG("internal_vkGetInstanceProcAddr called for: " + std::string(pName));

    // Handle only absolute essentials - let Mali driver handle everything else
    if (strcmp(pName, "vkGetInstanceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(internal_vkGetInstanceProcAddr);
    }

    if (strcmp(pName, "vkCreateInstance") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(internal_vkCreateInstance);
    }

    if (strcmp(pName, "vkDestroyInstance") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(internal_vkDestroyInstance);
    }

    // Forward everything else to Mali driver
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

    LOG_DEBUG("internal_vkGetDeviceProcAddr called for: " + std::string(pName));

    // Pure passthrough to Mali's vkGetDeviceProcAddr
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