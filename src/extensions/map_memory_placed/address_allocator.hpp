#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <vector>

namespace mali_wrapper {

struct AddressRange {
    void* start;
    size_t size;
    bool is_free;
    
    AddressRange(void* addr, size_t sz, bool free = true) 
        : start(addr), size(sz), is_free(free) {}
};

class VirtualAddressAllocator {
public:
    VirtualAddressAllocator(uint64_t base_address, uint64_t pool_size);
    ~VirtualAddressAllocator();
    
    void* AllocateAddress(size_t size, size_t alignment = 4096);
    void* AllocateSpecificAddress(void* preferred_address, size_t size);
    bool DeallocateAddress(void* address);
    
    bool IsAddressInPool(void* address) const;
    size_t GetTotalSize() const { return pool_size_; }
    size_t GetUsedSize() const;
    size_t GetFreeSize() const;
    
    void DumpState() const;
    
private:
    uint64_t base_address_;
    uint64_t pool_size_;
    void* pool_start_;
    
    std::vector<AddressRange> ranges_;
    std::unordered_map<void*, size_t> allocations_;
    mutable std::mutex mutex_;
    
    void* AlignAddress(void* address, size_t alignment) const;
    bool IsAligned(void* address, size_t alignment) const;
    void CoalesceRanges();
    void SortRanges();
    
    bool ReserveVirtualMemory(void* address, size_t size);
    void ReleaseVirtualMemory(void* address, size_t size);
};

} // namespace mali_wrapper