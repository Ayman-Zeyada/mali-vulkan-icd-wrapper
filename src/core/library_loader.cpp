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
    LOG_INFO("Loading Mali driver with build-time configuration");
    LOG_DEBUG("Mali driver: " + std::string(MALI_DRIVER_PATH));

    if (!LoadMaliDriver(MALI_DRIVER_PATH)) {
        return false;
    }

    LOG_INFO("Successfully loaded Mali driver");
    return true;
}

void LibraryLoader::UnloadLibraries() {
    if (mali_handle_) {
        dlclose(mali_handle_);
        mali_handle_ = nullptr;
        mali_get_instance_proc_addr_ = nullptr;
        mali_create_instance_ = nullptr;
    }

    LOG_DEBUG("Mali driver unloaded");
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



} // namespace mali_wrapper