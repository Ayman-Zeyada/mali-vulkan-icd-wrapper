#pragma once

#include <vulkan/vulkan.h>

namespace mali_wrapper {

// Initialize the wrapper ICD
bool InitializeWrapper();

// Shutdown the wrapper ICD
void ShutdownWrapper();


} // namespace mali_wrapper

extern "C" {

// Main ICD entry points - transparent bridge
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName);
VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion);

// Compatibility export
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName);

}