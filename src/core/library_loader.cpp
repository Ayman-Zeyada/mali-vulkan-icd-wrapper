#include "library_loader.hpp"
#include "config.hpp"
#include "../utils/logging.hpp"
#include <dlfcn.h>
#include <string>

namespace mali_wrapper {


LibraryLoader& LibraryLoader::Instance() {
    static LibraryLoader instance;
    return instance;
}

LibraryLoader::~LibraryLoader() {
    UnloadLibraries();
}

bool LibraryLoader::LoadLibraries() {
    LOG_INFO("Loading Mali driver and WSI layer with build-time configuration");
    LOG_DEBUG("Mali driver: " + std::string(MALI_DRIVER_PATH));
    LOG_DEBUG("WSI layer: " + std::string(WSI_LAYER_PATH));

    if (!LoadMaliDriver(MALI_DRIVER_PATH)) {
        return false;
    }

    if (!LoadWSILayer(WSI_LAYER_PATH)) {
        LOG_ERROR("Failed to load WSI layer, initialization aborted");
        return false;
    }

    LOG_INFO("Successfully loaded Mali driver and WSI layer");
    return true;
}

void LibraryLoader::UnloadLibraries() {
    if (wsi_handle_) {
        dlclose(wsi_handle_);
        wsi_handle_ = nullptr;
        wsi_get_instance_proc_addr_ = nullptr;
        wsi_get_device_proc_addr_ = nullptr;
        wsi_negotiate_interface_ = nullptr;
    }

    if (mali_handle_) {
        dlclose(mali_handle_);
        mali_handle_ = nullptr;
        mali_get_instance_proc_addr_ = nullptr;
        mali_create_instance_ = nullptr;
    }

    LOG_DEBUG("Mali driver and WSI layer unloaded");
}

bool LibraryLoader::LoadMaliDriver(const std::string& path) {
    mali_handle_ = LoadLibrary(path);
    if (!mali_handle_) {
        LOG_ERROR("Failed to load Mali driver: " + path);
        return false;
    }

    // Load the ICD entry point - this is what Mali driver exports
    mali_get_instance_proc_addr_ = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        GetSymbol(mali_handle_, "vk_icdGetInstanceProcAddr"));
    if (!mali_get_instance_proc_addr_) {
        LOG_ERROR("Failed to get vk_icdGetInstanceProcAddr from Mali driver");
        return false;
    }

    // Get vkCreateInstance through Mali's ICD proc addr - this is the ONLY function Mali supports
    mali_create_instance_ = reinterpret_cast<PFN_vkCreateInstance>(
        mali_get_instance_proc_addr_(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!mali_create_instance_) {
        LOG_ERROR("Failed to get vkCreateInstance from Mali driver");
        return false;
    }

    LOG_INFO("Mali driver loaded successfully - only vk_icdGetInstanceProcAddr and vkCreateInstance available");
    return true;
}


void* LibraryLoader::LoadLibrary(const std::string& path) {
    void* handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        LOG_ERROR("dlopen failed: " + std::string(dlerror()));
    }
    return handle;
}

void* LibraryLoader::GetSymbol(void* handle, const char* name) {
    if (!handle) {
        return nullptr;
    }

    void* symbol = dlsym(handle, name);
    if (!symbol) {
        LOG_DEBUG("dlsym failed for " + std::string(name) + ": " + std::string(dlerror()));
    }
    return symbol;
}

PFN_vkVoidFunction LibraryLoader::GetMaliProcAddr(const char* name) {
    if (!mali_get_instance_proc_addr_) {
        return nullptr;
    }

    // Only try Mali's ICD-specific proc addr function - Mali doesn't export other functions directly
    PFN_vkVoidFunction func = mali_get_instance_proc_addr_(VK_NULL_HANDLE, name);
    if (func) {
        LOG_DEBUG("Found Mali function " + std::string(name) + " via ICD proc addr");
        return func;
    }

    // Mali driver doesn't support this function - this is normal, not an error
    LOG_DEBUG("Mali function " + std::string(name) + " not available (expected for most functions)");
    return nullptr;
}

bool LibraryLoader::LoadWSILayer(const std::string& path) {
    wsi_handle_ = LoadLibrary(path);
    if (!wsi_handle_) {
        LOG_ERROR("Failed to load WSI layer: " + path);
        return false;
    }

    // Get the layer negotiation function
    wsi_negotiate_interface_ = reinterpret_cast<PFN_wsi_layer_vkNegotiateLoaderLayerInterfaceVersion>(
        GetSymbol(wsi_handle_, "wsi_layer_vkNegotiateLoaderLayerInterfaceVersion"));
    if (!wsi_negotiate_interface_) {
        LOG_ERROR("Failed to get negotiation interface from WSI layer");
        return false;
    }

    // Create negotiation structure
    VkNegotiateLayerInterface negotiate_struct = {};
    negotiate_struct.sType = LAYER_NEGOTIATE_INTERFACE_STRUCT;
    negotiate_struct.loaderLayerInterfaceVersion = 2;  // We support version 2
    negotiate_struct.pNext = nullptr;

    VkResult result = wsi_negotiate_interface_(&negotiate_struct);
    if (result != VK_SUCCESS) {
        LOG_ERROR("WSI layer interface negotiation failed: " + std::to_string(result));
        return false;
    }

    LOG_DEBUG("WSI layer interface version negotiated: " + std::to_string(negotiate_struct.loaderLayerInterfaceVersion));

    // Get function pointers from negotiation result
    wsi_get_instance_proc_addr_ = negotiate_struct.pfnGetInstanceProcAddr;
    wsi_get_device_proc_addr_ = negotiate_struct.pfnGetDeviceProcAddr;

    if (!wsi_get_instance_proc_addr_ || !wsi_get_device_proc_addr_) {
        LOG_ERROR("WSI layer negotiation returned null function pointers");
        return false;
    }

    LOG_INFO("WSI layer loaded and negotiated successfully");
    return true;
}

} // namespace mali_wrapper