#pragma once

#include <vulkan/vulkan.h>
#include <string>

namespace mali_wrapper {

class LibraryLoader {
public:
    static LibraryLoader& Instance();

    bool LoadLibraries();
    void UnloadLibraries();

    PFN_vkVoidFunction GetMaliProcAddr(const char* name);
    bool IsLoaded() const { return mali_handle_ != nullptr; }

    PFN_vkGetInstanceProcAddr GetMaliGetInstanceProcAddr() const { return mali_get_instance_proc_addr_; }
    PFN_vkCreateInstance GetMaliCreateInstance() const { return mali_create_instance_; }

private:
    LibraryLoader() = default;
    ~LibraryLoader();
    LibraryLoader(const LibraryLoader&) = delete;
    LibraryLoader& operator=(const LibraryLoader&) = delete;

    void* mali_handle_ = nullptr;

    PFN_vkGetInstanceProcAddr mali_get_instance_proc_addr_ = nullptr;
    PFN_vkCreateInstance mali_create_instance_ = nullptr;

    bool LoadMaliDriver(const std::string& path);

    void* LoadLibrary(const std::string& path);
    void* GetSymbol(void* handle, const char* name);
};

} // namespace mali_wrapper