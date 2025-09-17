#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <string>

namespace mali_wrapper {

class LibraryLoader {
public:
    static LibraryLoader& Instance();

    bool LoadLibraries();
    void UnloadLibraries();

    PFN_vkVoidFunction GetMaliProcAddr(const char* name);
    bool IsLoaded() const { return mali_handle_ != nullptr && wsi_handle_ != nullptr; }

    PFN_vkGetInstanceProcAddr GetMaliGetInstanceProcAddr() const { return mali_get_instance_proc_addr_; }
    PFN_vkCreateInstance GetMaliCreateInstance() const { return mali_create_instance_; }

    // WSI Layer support
    PFN_vkGetInstanceProcAddr GetWSIGetInstanceProcAddr() const { return wsi_get_instance_proc_addr_; }
    PFN_vkGetDeviceProcAddr GetWSIGetDeviceProcAddr() const { return wsi_get_device_proc_addr_; }
    void* GetWSILibraryHandle() const { return wsi_handle_; }

private:
    LibraryLoader() = default;
    ~LibraryLoader();
    LibraryLoader(const LibraryLoader&) = delete;
    LibraryLoader& operator=(const LibraryLoader&) = delete;

    void* mali_handle_ = nullptr;
    void* wsi_handle_ = nullptr;

    PFN_vkGetInstanceProcAddr mali_get_instance_proc_addr_ = nullptr;
    PFN_vkCreateInstance mali_create_instance_ = nullptr;

    // WSI Layer function pointers
    PFN_vkGetInstanceProcAddr wsi_get_instance_proc_addr_ = nullptr;
    PFN_vkGetDeviceProcAddr wsi_get_device_proc_addr_ = nullptr;

    // WSI Layer negotiation interface
    typedef VkResult (*PFN_wsi_layer_vkNegotiateLoaderLayerInterfaceVersion)(VkNegotiateLayerInterface *pVersionStruct);
    PFN_wsi_layer_vkNegotiateLoaderLayerInterfaceVersion wsi_negotiate_interface_ = nullptr;

    bool LoadMaliDriver(const std::string& path);
    bool LoadWSILayer(const std::string& path);

    void* LoadLibrary(const std::string& path);
    void* GetSymbol(void* handle, const char* name);
};

} // namespace mali_wrapper