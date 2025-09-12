#include "map_memory_placed.hpp"
#include "memory_mapper.hpp"
#include "address_allocator.hpp"
#include "../../core/vulkan_dispatch.hpp"
#include "../../utils/logging.hpp"
#include "../../utils/config.hpp"
#include <cstring>

namespace mali_wrapper {

static MapMemoryPlacedExtension* g_extension_instance = nullptr;

MapMemoryPlacedExtension::MapMemoryPlacedExtension() 
    : real_vkMapMemory_(nullptr), real_vkUnmapMemory_(nullptr) {
    extension_name_ = VK_EXT_MAP_MEMORY_PLACED_EXTENSION_NAME;
    spec_version_ = VK_EXT_MAP_MEMORY_PLACED_SPEC_VERSION;
    g_extension_instance = this;
}

MapMemoryPlacedExtension::~MapMemoryPlacedExtension() {
    if (g_extension_instance == this) {
        g_extension_instance = nullptr;
    }
}

const char* MapMemoryPlacedExtension::GetName() const {
    return VK_EXT_MAP_MEMORY_PLACED_EXTENSION_NAME;
}

uint32_t MapMemoryPlacedExtension::GetSpecVersion() const {
    return VK_EXT_MAP_MEMORY_PLACED_SPEC_VERSION;
}

VkResult MapMemoryPlacedExtension::Initialize(VkInstance instance, VkDevice device) {
    VkResult result = BaseExtension::Initialize(instance, device);
    if (result != VK_SUCCESS) {
        return result;
    }
    
    LOG_INFO("Initializing VK_EXT_map_memory_placed extension");
    
    if (device != VK_NULL_HANDLE) {
        real_vkMapMemory_ = VulkanDispatch::Instance().GetDeviceFunction<PFN_vkMapMemory>(device, "vkMapMemory");
        real_vkUnmapMemory_ = VulkanDispatch::Instance().GetDeviceFunction<PFN_vkUnmapMemory>(device, "vkUnmapMemory");
        
        if (!real_vkMapMemory_ || !real_vkUnmapMemory_) {
            LOG_ERROR("Failed to get real Mali memory mapping functions");
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        
        uint64_t virtual_base = Config::Instance().GetUInt64Value(
            "VK_EXT_map_memory_placed", "virtual_address_base", 0x1000000000ULL);
        uint64_t pool_size = Config::Instance().GetUInt64Value(
            "VK_EXT_map_memory_placed", "address_pool_size", 0x100000000ULL);
        
        address_allocator_ = std::make_unique<VirtualAddressAllocator>(virtual_base, pool_size);
        memory_mapper_ = std::make_unique<MemoryMapper>(device, real_vkMapMemory_, real_vkUnmapMemory_);
        memory_mapper_->SetAddressAllocator(address_allocator_.get());
        
        LOG_INFO("VK_EXT_map_memory_placed extension initialized for device");
    }
    
    return VK_SUCCESS;
}

void MapMemoryPlacedExtension::Shutdown() {
    LOG_INFO("Shutting down VK_EXT_map_memory_placed extension");
    
    memory_mappings_.clear();
    memory_mapper_.reset();
    address_allocator_.reset();
    
    real_vkMapMemory_ = nullptr;
    real_vkUnmapMemory_ = nullptr;
    
    BaseExtension::Shutdown();
}

PFN_vkVoidFunction MapMemoryPlacedExtension::GetProcAddr(const char* name) {
    if (!name) {
        return nullptr;
    }
    
    if (strcmp(name, "vkMapMemory2KHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(StaticMapMemory2KHR);
    }
    if (strcmp(name, "vkUnmapMemory2KHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(StaticUnmapMemory2KHR);
    }
    
    return nullptr;
}

bool MapMemoryPlacedExtension::InterceptsFunction(const char* name) const {
    if (!name) {
        return false;
    }
    
    return strcmp(name, "vkMapMemory2KHR") == 0 || 
           strcmp(name, "vkUnmapMemory2KHR") == 0;
}

void MapMemoryPlacedExtension::ModifyPhysicalDeviceFeatures2(VkPhysicalDeviceFeatures2* features) {
    if (!features) {
        return;
    }
    
    LOG_DEBUG("Modifying physical device features for VK_EXT_map_memory_placed");
}

VkResult MapMemoryPlacedExtension::MapMemory2KHR(VkDevice device, 
                                                 const VkMemoryMapInfoKHR* pMemoryMapInfo, 
                                                 void** ppData) {
    (void)device;
    if (!pMemoryMapInfo || !ppData) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    if (!memory_mapper_) {
        LOG_ERROR("Memory mapper not initialized");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    LOG_DEBUG("VK_EXT_map_memory_placed: Mapping memory with placed address support");
    
    const VkMemoryMapPlacedInfoEXT* placed_info = nullptr;
    const void* pNext = pMemoryMapInfo->pNext;
    
    while (pNext) {
        const VkBaseInStructure* base = static_cast<const VkBaseInStructure*>(pNext);
        if (base->sType == VK_STRUCTURE_TYPE_MEMORY_MAP_PLACED_INFO_EXT) {
            placed_info = static_cast<const VkMemoryMapPlacedInfoEXT*>(pNext);
            break;
        }
        pNext = base->pNext;
    }
    
    void* mapped_address = nullptr;
    VkResult result = memory_mapper_->MapMemory(
        pMemoryMapInfo->memory,
        pMemoryMapInfo->offset,
        pMemoryMapInfo->size,
        placed_info ? placed_info->pPlacedAddress : nullptr,
        &mapped_address
    );
    
    if (result == VK_SUCCESS) {
        *ppData = mapped_address;
        memory_mappings_[pMemoryMapInfo->memory] = mapped_address;
        LOG_DEBUG("Memory mapped successfully to address: " + 
                  std::to_string(reinterpret_cast<uintptr_t>(mapped_address)));
    }
    
    return result;
}

VkResult MapMemoryPlacedExtension::UnmapMemory2KHR(VkDevice device, 
                                                   const VkMemoryUnmapInfoKHR* pMemoryUnmapInfo) {
    (void)device;
    if (!pMemoryUnmapInfo) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    if (!memory_mapper_) {
        LOG_ERROR("Memory mapper not initialized");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    
    LOG_DEBUG("VK_EXT_map_memory_placed: Unmapping memory");
    
    auto it = memory_mappings_.find(pMemoryUnmapInfo->memory);
    if (it == memory_mappings_.end()) {
        LOG_WARN("Attempting to unmap memory that was not mapped by this extension");
        return VK_SUCCESS;
    }
    
    VkResult result = memory_mapper_->UnmapMemory(pMemoryUnmapInfo->memory);
    
    if (result == VK_SUCCESS) {
        memory_mappings_.erase(it);
        LOG_DEBUG("Memory unmapped successfully");
    }
    
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL MapMemoryPlacedExtension::StaticMapMemory2KHR(
    VkDevice device, 
    const VkMemoryMapInfoKHR* pMemoryMapInfo, 
    void** ppData) {
    
    MapMemoryPlacedExtension* instance = GetInstance(device);
    if (!instance) {
        LOG_ERROR("VK_EXT_map_memory_placed extension not available for device");
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    
    return instance->MapMemory2KHR(device, pMemoryMapInfo, ppData);
}

VKAPI_ATTR VkResult VKAPI_CALL MapMemoryPlacedExtension::StaticUnmapMemory2KHR(
    VkDevice device, 
    const VkMemoryUnmapInfoKHR* pMemoryUnmapInfo) {
    
    MapMemoryPlacedExtension* instance = GetInstance(device);
    if (!instance) {
        LOG_ERROR("VK_EXT_map_memory_placed extension not available for device");
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    
    return instance->UnmapMemory2KHR(device, pMemoryUnmapInfo);
}

MapMemoryPlacedExtension* MapMemoryPlacedExtension::GetInstance(VkDevice device) {
    (void)device; // TODO: Implement proper per-device instance lookup
    return g_extension_instance;
}

} // namespace mali_wrapper