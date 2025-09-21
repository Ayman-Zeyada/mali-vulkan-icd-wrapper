#pragma once

#include <vulkan/vulkan.h>
#include "wsi_manager.hpp"

namespace mali_wrapper {

bool InitializeWrapper();

void ShutdownWrapper();

WSIManager& GetWSIManager();


} // namespace mali_wrapper

extern "C" {

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName);
VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion);

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName);

}