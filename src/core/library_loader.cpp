#include "library_loader.hpp"
#include "config.hpp"
#include "../utils/logging.hpp"
#include <dlfcn.h>
#include <string>
#include <vulkan/vk_layer.h>

namespace mali_wrapper {


LibraryLoader& LibraryLoader::Instance() {
    static LibraryLoader instance;
    return instance;
}

LibraryLoader::~LibraryLoader() {
    UnloadLibraries();
}

bool LibraryLoader::LoadLibraries() {
    LOG_INFO("Loading Mali driver (WSI layer is integrated directly)");

    if (!LoadMaliDriver(MALI_DRIVER_PATH)) {
        return false;
    }

    LOG_INFO("Successfully loaded Mali driver (WSI integrated)");
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

}

bool LibraryLoader::LoadMaliDriver(const std::string& path) {
    mali_handle_ = LoadLibrary(path);
    if (!mali_handle_) {
        LOG_ERROR("Failed to load Mali driver: " + path);
        return false;
    }

    mali_get_instance_proc_addr_ = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        GetSymbol(mali_handle_, "vk_icdGetInstanceProcAddr"));
    if (!mali_get_instance_proc_addr_) {
        LOG_ERROR("Failed to get vk_icdGetInstanceProcAddr from Mali driver");
        return false;
    }

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
    }
    return symbol;
}

PFN_vkVoidFunction LibraryLoader::GetMaliProcAddr(const char* name) {
    if (!mali_get_instance_proc_addr_) {
        return nullptr;
    }

    PFN_vkVoidFunction func = mali_get_instance_proc_addr_(VK_NULL_HANDLE, name);
    if (func) {
        return func;
    }

    return nullptr;
}

PFN_vkVoidFunction LibraryLoader::GetMaliProcAddr(VkInstance instance, const char* name) {
    if (!mali_get_instance_proc_addr_) {
        return nullptr;
    }

    PFN_vkVoidFunction func = mali_get_instance_proc_addr_(instance, name);
    if (func) {
        return func;
    }

    func = mali_get_instance_proc_addr_(VK_NULL_HANDLE, name);
    if (func) {
        return func;
    }

    return nullptr;
}

} // namespace mali_wrapper