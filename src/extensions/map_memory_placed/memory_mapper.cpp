#include "memory_mapper.hpp"
#include "address_allocator.hpp"
#include "../../utils/logging.hpp"
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

namespace mali_wrapper {

MemoryMapper::MemoryMapper(VkDevice device, PFN_vkMapMemory mali_map_func, PFN_vkUnmapMemory mali_unmap_func)
    : device_(device), mali_map_memory_(mali_map_func), mali_unmap_memory_(mali_unmap_func), 
      address_allocator_(nullptr) {
    
    LOG_INFO("Memory mapper initialized for device");
}

MemoryMapper::~MemoryMapper() {
    std::lock_guard<std::mutex> lock(mappings_mutex_);
    
    for (const auto& pair : mappings_) {
        const MappingInfo& mapping = pair.second;
        if (mapping.is_placed) {
            DestroyPlacedMapping(mapping);
        }
        mali_unmap_memory_(device_, mapping.memory);
    }
    
    mappings_.clear();
    LOG_INFO("Memory mapper destroyed");
}

void MemoryMapper::SetAddressAllocator(VirtualAddressAllocator* allocator) {
    address_allocator_ = allocator;
    LOG_DEBUG("Address allocator set for memory mapper");
}

VkResult MemoryMapper::MapMemory(VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, 
                                void* preferred_address, void** ppMappedAddress) {
    if (!ppMappedAddress) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    std::lock_guard<std::mutex> lock(mappings_mutex_);
    
    if (mappings_.find(memory) != mappings_.end()) {
        LOG_WARN("Memory already mapped, returning existing mapping");
        *ppMappedAddress = mappings_[memory].virtual_address;
        return VK_SUCCESS;
    }
    
    void* mali_address = nullptr;
    VkResult result = mali_map_memory_(device_, memory, offset, size, 0, &mali_address);
    if (result != VK_SUCCESS) {
        LOG_ERROR("Failed to map memory with Mali driver: " + std::to_string(result));
        return result;
    }
    
    if (!mali_address) {
        LOG_ERROR("Mali driver returned null mapped address");
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    
    void* virtual_address = mali_address;
    bool is_placed = false;
    
    if (preferred_address && address_allocator_) {
        void* allocated_address = address_allocator_->AllocateSpecificAddress(preferred_address, size);
        if (allocated_address == preferred_address) {
            virtual_address = preferred_address;
            is_placed = true;
            
            result = CreatePlacedMapping(memory, offset, size, mali_address, virtual_address);
            if (result != VK_SUCCESS) {
                address_allocator_->DeallocateAddress(virtual_address);
                mali_unmap_memory_(device_, memory);
                return result;
            }
        } else {
            LOG_WARN("Could not allocate preferred address, falling back to Mali address");
        }
    } else if (address_allocator_ && !preferred_address) {
        void* allocated_address = address_allocator_->AllocateAddress(size);
        if (allocated_address) {
            virtual_address = allocated_address;
            is_placed = true;
            
            result = CreatePlacedMapping(memory, offset, size, mali_address, virtual_address);
            if (result != VK_SUCCESS) {
                address_allocator_->DeallocateAddress(virtual_address);
                mali_unmap_memory_(device_, memory);
                return result;
            }
        }
    }
    
    mappings_[memory] = MappingInfo(memory, offset, size, mali_address, virtual_address, is_placed);
    *ppMappedAddress = virtual_address;
    
    LOG_DEBUG("Memory mapped: Mali=" + std::to_string(reinterpret_cast<uintptr_t>(mali_address)) +
              " Virtual=" + std::to_string(reinterpret_cast<uintptr_t>(virtual_address)) +
              " Size=" + std::to_string(size) + " Placed=" + (is_placed ? "true" : "false"));
    
    return VK_SUCCESS;
}

VkResult MemoryMapper::UnmapMemory(VkDeviceMemory memory) {
    std::lock_guard<std::mutex> lock(mappings_mutex_);
    
    auto it = mappings_.find(memory);
    if (it == mappings_.end()) {
        LOG_WARN("Attempting to unmap memory that was not mapped");
        return VK_SUCCESS;
    }
    
    const MappingInfo& mapping = it->second;
    
    if (mapping.is_placed) {
        DestroyPlacedMapping(mapping);
        if (address_allocator_) {
            address_allocator_->DeallocateAddress(mapping.virtual_address);
        }
    }
    
    mali_unmap_memory_(device_, memory);
    mappings_.erase(it);
    
    LOG_DEBUG("Memory unmapped successfully");
    return VK_SUCCESS;
}

bool MemoryMapper::IsMemoryMapped(VkDeviceMemory memory) const {
    std::lock_guard<std::mutex> lock(mappings_mutex_);
    return mappings_.find(memory) != mappings_.end();
}

void* MemoryMapper::GetMappedAddress(VkDeviceMemory memory) const {
    std::lock_guard<std::mutex> lock(mappings_mutex_);
    auto it = mappings_.find(memory);
    return (it != mappings_.end()) ? it->second.virtual_address : nullptr;
}

VkResult MemoryMapper::CreatePlacedMapping(VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size,
                                          void* mali_address, void* virtual_address) {
    (void)memory;
    (void)offset;
    if (!SetupMemoryRedirection(mali_address, virtual_address, size)) {
        LOG_ERROR("Failed to setup memory redirection");
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    
    return VK_SUCCESS;
}

void MemoryMapper::DestroyPlacedMapping(const MappingInfo& mapping) {
    CleanupMemoryRedirection(mapping.mali_address, mapping.virtual_address, mapping.size);
}

bool MemoryMapper::SetupMemoryRedirection(void* mali_address, void* virtual_address, size_t size) {
    if (mali_address == virtual_address) {
        return true;
    }
    
    void* mapped = mmap(virtual_address, size, PROT_READ | PROT_WRITE, 
                       MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapped == MAP_FAILED) {
        LOG_ERROR("Failed to map virtual address: " + std::string(strerror(errno)));
        return false;
    }
    
    if (mapped != virtual_address) {
        LOG_ERROR("mmap did not return requested address");
        munmap(mapped, size);
        return false;
    }
    
    if (mprotect(virtual_address, size, PROT_READ | PROT_WRITE) != 0) {
        LOG_ERROR("Failed to set memory protection: " + std::string(strerror(errno)));
        munmap(virtual_address, size);
        return false;
    }
    
    LOG_DEBUG("Memory redirection setup complete");
    return true;
}

void MemoryMapper::CleanupMemoryRedirection(void* mali_address, void* virtual_address, size_t size) {
    if (mali_address == virtual_address) {
        return;
    }
    
    if (munmap(virtual_address, size) != 0) {
        LOG_ERROR("Failed to unmap virtual address: " + std::string(strerror(errno)));
    }
    
    LOG_DEBUG("Memory redirection cleanup complete");
}

void MemoryMapper::DumpMappings() const {
    std::lock_guard<std::mutex> lock(mappings_mutex_);
    
    LOG_INFO("Active memory mappings:");
    for (const auto& pair : mappings_) {
        const MappingInfo& mapping = pair.second;
        LOG_INFO("  Memory=" + std::to_string(reinterpret_cast<uintptr_t>(mapping.memory)) +
                 " Mali=" + std::to_string(reinterpret_cast<uintptr_t>(mapping.mali_address)) +
                 " Virtual=" + std::to_string(reinterpret_cast<uintptr_t>(mapping.virtual_address)) +
                 " Size=" + std::to_string(mapping.size) +
                 " Placed=" + (mapping.is_placed ? "true" : "false"));
    }
}

} // namespace mali_wrapper