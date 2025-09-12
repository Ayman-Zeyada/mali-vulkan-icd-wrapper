#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <mutex>

namespace mali_wrapper {

class VirtualAddressAllocator;

struct MappingInfo {
    VkDeviceMemory memory;
    VkDeviceSize offset;
    VkDeviceSize size;
    void* mali_address;
    void* virtual_address;
    bool is_placed;
    
    MappingInfo() = default;
    
    MappingInfo(VkDeviceMemory mem, VkDeviceSize off, VkDeviceSize sz, 
                void* mali_addr, void* virt_addr, bool placed)
        : memory(mem), offset(off), size(sz), mali_address(mali_addr), 
          virtual_address(virt_addr), is_placed(placed) {}
};

class MemoryMapper {
public:
    MemoryMapper(VkDevice device, PFN_vkMapMemory mali_map_func, PFN_vkUnmapMemory mali_unmap_func);
    ~MemoryMapper();
    
    VkResult MapMemory(VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, 
                      void* preferred_address, void** ppMappedAddress);
    
    VkResult UnmapMemory(VkDeviceMemory memory);
    
    bool IsMemoryMapped(VkDeviceMemory memory) const;
    void* GetMappedAddress(VkDeviceMemory memory) const;
    
    void SetAddressAllocator(VirtualAddressAllocator* allocator);
    
    void DumpMappings() const;
    
private:
    VkDevice device_;
    PFN_vkMapMemory mali_map_memory_;
    PFN_vkUnmapMemory mali_unmap_memory_;
    VirtualAddressAllocator* address_allocator_;
    
    std::unordered_map<VkDeviceMemory, MappingInfo> mappings_;
    mutable std::mutex mappings_mutex_;
    
    VkResult CreatePlacedMapping(VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size,
                                void* mali_address, void* virtual_address);
    void DestroyPlacedMapping(const MappingInfo& mapping);
    
    bool SetupMemoryRedirection(void* mali_address, void* virtual_address, size_t size);
    void CleanupMemoryRedirection(void* mali_address, void* virtual_address, size_t size);
};

} // namespace mali_wrapper