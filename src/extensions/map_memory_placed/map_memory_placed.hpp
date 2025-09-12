#pragma once

#include "../base_extension.hpp"
#include <memory>
#include <unordered_map>

#ifndef VK_EXT_map_memory_placed
#define VK_EXT_map_memory_placed 1
#define VK_EXT_MAP_MEMORY_PLACED_SPEC_VERSION 1
#define VK_EXT_MAP_MEMORY_PLACED_EXTENSION_NAME "VK_EXT_map_memory_placed"

#ifndef VK_STRUCTURE_TYPE_MEMORY_MAP_PLACED_INFO_EXT
#define VK_STRUCTURE_TYPE_MEMORY_MAP_PLACED_INFO_EXT ((VkStructureType)1000456000)
#endif

typedef struct VkMemoryMapPlacedInfoEXT {
    VkStructureType    sType;
    const void*        pNext;
    void*              pPlacedAddress;
} VkMemoryMapPlacedInfoEXT;

typedef VkResult (VKAPI_PTR *PFN_vkMapMemory2KHR)(
    VkDevice                        device,
    const VkMemoryMapInfoKHR*       pMemoryMapInfo,
    void**                          ppData);

typedef VkResult (VKAPI_PTR *PFN_vkUnmapMemory2KHR)(
    VkDevice                        device,
    const VkMemoryUnmapInfoKHR*     pMemoryUnmapInfo);

#endif

namespace mali_wrapper {

class VirtualAddressAllocator;
class MemoryMapper;

class MapMemoryPlacedExtension : public BaseExtension {
public:
    MapMemoryPlacedExtension();
    ~MapMemoryPlacedExtension() override;
    
    const char* GetName() const override;
    uint32_t GetSpecVersion() const override;
    
    VkResult Initialize(VkInstance instance, VkDevice device = VK_NULL_HANDLE) override;
    void Shutdown() override;
    
    PFN_vkVoidFunction GetProcAddr(const char* name) override;
    bool InterceptsFunction(const char* name) const override;
    
    void ModifyPhysicalDeviceFeatures2(VkPhysicalDeviceFeatures2* features) override;
    
    VkResult MapMemory2KHR(VkDevice device, const VkMemoryMapInfoKHR* pMemoryMapInfo, void** ppData);
    VkResult UnmapMemory2KHR(VkDevice device, const VkMemoryUnmapInfoKHR* pMemoryUnmapInfo);
    
    static VKAPI_ATTR VkResult VKAPI_CALL StaticMapMemory2KHR(
        VkDevice device, const VkMemoryMapInfoKHR* pMemoryMapInfo, void** ppData);
    static VKAPI_ATTR VkResult VKAPI_CALL StaticUnmapMemory2KHR(
        VkDevice device, const VkMemoryUnmapInfoKHR* pMemoryUnmapInfo);
    
private:
    std::unique_ptr<VirtualAddressAllocator> address_allocator_;
    std::unique_ptr<MemoryMapper> memory_mapper_;
    
    PFN_vkMapMemory real_vkMapMemory_;
    PFN_vkUnmapMemory real_vkUnmapMemory_;
    
    std::unordered_map<VkDeviceMemory, void*> memory_mappings_;
    
    static MapMemoryPlacedExtension* GetInstance(VkDevice device);
};

} // namespace mali_wrapper