#include "address_allocator.hpp"
#include "../../utils/logging.hpp"
#include <sys/mman.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>

namespace mali_wrapper {

VirtualAddressAllocator::VirtualAddressAllocator(uint64_t base_address, uint64_t pool_size) 
    : base_address_(base_address), pool_size_(pool_size) {
    
    pool_start_ = reinterpret_cast<void*>(base_address);
    
    if (!ReserveVirtualMemory(pool_start_, pool_size)) {
        LOG_ERROR("Failed to reserve virtual memory pool");
        pool_start_ = nullptr;
        pool_size_ = 0;
        return;
    }
    
    ranges_.emplace_back(pool_start_, pool_size, true);
    
    LOG_INFO("Virtual address allocator initialized: base=0x" + 
             std::to_string(base_address) + " size=0x" + std::to_string(pool_size));
}

VirtualAddressAllocator::~VirtualAddressAllocator() {
    if (pool_start_) {
        ReleaseVirtualMemory(pool_start_, pool_size_);
    }
    LOG_INFO("Virtual address allocator destroyed");
}

void* VirtualAddressAllocator::AllocateAddress(size_t size, size_t alignment) {
    if (size == 0 || !pool_start_) {
        return nullptr;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& range : ranges_) {
        if (!range.is_free || range.size < size) {
            continue;
        }
        
        void* aligned_start = AlignAddress(range.start, alignment);
        size_t offset = static_cast<char*>(aligned_start) - static_cast<char*>(range.start);
        
        if (offset + size > range.size) {
            continue;
        }
        
        if (offset > 0) {
            ranges_.insert(ranges_.begin() + (&range - &ranges_[0]), 
                          AddressRange(range.start, offset, true));
            range.start = aligned_start;
            range.size -= offset;
        }
        
        if (range.size > size) {
            void* remaining_start = static_cast<char*>(aligned_start) + size;
            size_t remaining_size = range.size - size;
            ranges_.insert(ranges_.begin() + (&range - &ranges_[0]) + 1, 
                          AddressRange(remaining_start, remaining_size, true));
        }
        
        range.size = size;
        range.is_free = false;
        
        allocations_[aligned_start] = size;
        
        LOG_DEBUG("Allocated address: 0x" + std::to_string(reinterpret_cast<uintptr_t>(aligned_start)) +
                  " size: " + std::to_string(size));
        
        return aligned_start;
    }
    
    LOG_WARN("Failed to allocate address of size " + std::to_string(size));
    return nullptr;
}

void* VirtualAddressAllocator::AllocateSpecificAddress(void* preferred_address, size_t size) {
    if (!preferred_address || size == 0 || !IsAddressInPool(preferred_address)) {
        return nullptr;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& range : ranges_) {
        if (!range.is_free) {
            continue;
        }
        
        char* range_start = static_cast<char*>(range.start);
        char* range_end = range_start + range.size;
        char* preferred_start = static_cast<char*>(preferred_address);
        char* preferred_end = preferred_start + size;
        
        if (preferred_start < range_start || preferred_end > range_end) {
            continue;
        }
        
        size_t offset_before = preferred_start - range_start;
        size_t offset_after = range_end - preferred_end;
        
        if (offset_before > 0) {
            ranges_.insert(ranges_.begin() + (&range - &ranges_[0]), 
                          AddressRange(range.start, offset_before, true));
        }
        
        if (offset_after > 0) {
            ranges_.insert(ranges_.begin() + (&range - &ranges_[0]) + (offset_before > 0 ? 2 : 1), 
                          AddressRange(preferred_end, offset_after, true));
        }
        
        range.start = preferred_address;
        range.size = size;
        range.is_free = false;
        
        allocations_[preferred_address] = size;
        
        LOG_DEBUG("Allocated specific address: 0x" + 
                  std::to_string(reinterpret_cast<uintptr_t>(preferred_address)) +
                  " size: " + std::to_string(size));
        
        return preferred_address;
    }
    
    LOG_WARN("Failed to allocate specific address: 0x" + 
             std::to_string(reinterpret_cast<uintptr_t>(preferred_address)));
    return nullptr;
}

bool VirtualAddressAllocator::DeallocateAddress(void* address) {
    if (!address) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto alloc_it = allocations_.find(address);
    if (alloc_it == allocations_.end()) {
        LOG_WARN("Attempting to deallocate address that was not allocated: 0x" +
                 std::to_string(reinterpret_cast<uintptr_t>(address)));
        return false;
    }
    
    size_t size = alloc_it->second;
    allocations_.erase(alloc_it);
    
    for (auto& range : ranges_) {
        if (range.start == address && range.size == size && !range.is_free) {
            range.is_free = true;
            CoalesceRanges();
            
            LOG_DEBUG("Deallocated address: 0x" + 
                      std::to_string(reinterpret_cast<uintptr_t>(address)) +
                      " size: " + std::to_string(size));
            return true;
        }
    }
    
    LOG_ERROR("Address allocation inconsistency detected");
    return false;
}

bool VirtualAddressAllocator::IsAddressInPool(void* address) const {
    if (!pool_start_ || !address) {
        return false;
    }
    
    char* addr = static_cast<char*>(address);
    char* pool_start = static_cast<char*>(pool_start_);
    char* pool_end = pool_start + pool_size_;
    
    return addr >= pool_start && addr < pool_end;
}

size_t VirtualAddressAllocator::GetUsedSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t used = 0;
    for (const auto& range : ranges_) {
        if (!range.is_free) {
            used += range.size;
        }
    }
    
    return used;
}

size_t VirtualAddressAllocator::GetFreeSize() const {
    return pool_size_ - GetUsedSize();
}

void VirtualAddressAllocator::DumpState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    LOG_INFO("Virtual address allocator state:");
    LOG_INFO("  Pool: 0x" + std::to_string(base_address_) + " - 0x" + 
             std::to_string(base_address_ + pool_size_) + " (size: " + std::to_string(pool_size_) + ")");
    LOG_INFO("  Used: " + std::to_string(GetUsedSize()) + " Free: " + std::to_string(GetFreeSize()));
    
    for (const auto& range : ranges_) {
        LOG_INFO("  Range: 0x" + std::to_string(reinterpret_cast<uintptr_t>(range.start)) +
                 " size: " + std::to_string(range.size) + 
                 " " + (range.is_free ? "FREE" : "USED"));
    }
}

void* VirtualAddressAllocator::AlignAddress(void* address, size_t alignment) const {
    if (alignment <= 1) {
        return address;
    }
    
    uintptr_t addr = reinterpret_cast<uintptr_t>(address);
    uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
    return reinterpret_cast<void*>(aligned);
}

bool VirtualAddressAllocator::IsAligned(void* address, size_t alignment) const {
    if (alignment <= 1) {
        return true;
    }
    
    uintptr_t addr = reinterpret_cast<uintptr_t>(address);
    return (addr & (alignment - 1)) == 0;
}

void VirtualAddressAllocator::CoalesceRanges() {
    if (ranges_.empty()) {
        return;
    }
    
    SortRanges();
    
    for (size_t i = 0; i < ranges_.size() - 1; ) {
        if (ranges_[i].is_free && ranges_[i + 1].is_free) {
            char* end_of_current = static_cast<char*>(ranges_[i].start) + ranges_[i].size;
            char* start_of_next = static_cast<char*>(ranges_[i + 1].start);
            
            if (end_of_current == start_of_next) {
                ranges_[i].size += ranges_[i + 1].size;
                ranges_.erase(ranges_.begin() + i + 1);
                continue;
            }
        }
        ++i;
    }
}

void VirtualAddressAllocator::SortRanges() {
    std::sort(ranges_.begin(), ranges_.end(), 
             [](const AddressRange& a, const AddressRange& b) {
                 return a.start < b.start;
             });
}

bool VirtualAddressAllocator::ReserveVirtualMemory(void* address, size_t size) {
    void* result = mmap(address, size, PROT_NONE, 
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    
    if (result == MAP_FAILED) {
        LOG_ERROR("Failed to reserve virtual memory: " + std::string(strerror(errno)));
        return false;
    }
    
    if (result != address) {
        munmap(result, size);
        LOG_ERROR("Could not reserve virtual memory at requested address");
        return false;
    }
    
    return true;
}

void VirtualAddressAllocator::ReleaseVirtualMemory(void* address, size_t size) {
    if (munmap(address, size) != 0) {
        LOG_ERROR("Failed to release virtual memory: " + std::string(strerror(errno)));
    }
}

} // namespace mali_wrapper