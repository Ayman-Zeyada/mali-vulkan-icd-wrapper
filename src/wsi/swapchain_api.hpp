/*
 * Copyright (c) 2018-2019, 2024 Arm Limited.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file swapchain_api.hpp
 *
 * @brief Contains the internal WSI swapchain implementation functions.
 */

#pragma once

#include <vulkan/vulkan.h>

namespace mali_wrapper {

// Internal WSI swapchain API for direct integration
VkResult wsi_CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *pSwapchainCreateInfo,
                               const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain);

void wsi_DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapc,
                            const VkAllocationCallbacks *pAllocator);

VkResult wsi_GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapc, uint32_t *pSwapchainImageCount,
                                  VkImage *pSwapchainImages);

VkResult wsi_AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapc, uint64_t timeout, VkSemaphore semaphore,
                                VkFence fence, uint32_t *pImageIndex);

VkResult wsi_QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo);

// 1.1 entrypoints
VkResult wsi_GetDeviceGroupPresentCapabilitiesKHR(VkDevice device,
                                                 VkDeviceGroupPresentCapabilitiesKHR *pDeviceGroupPresentCapabilities);

VkResult wsi_GetDeviceGroupSurfacePresentModesKHR(VkDevice device, VkSurfaceKHR surface,
                                                 VkDeviceGroupPresentModeFlagsKHR *pModes);

VkResult wsi_GetPhysicalDevicePresentRectanglesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                                  uint32_t *pRectCount, VkRect2D *pRects);

VkResult wsi_AcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR *pAcquireInfo,
                                 uint32_t *pImageIndex);

VkResult wsi_CreateImage(VkDevice device, const VkImageCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator,
                        VkImage *pImage);

VkResult wsi_BindImageMemory2(VkDevice device, uint32_t bindInfoCount,
                             const VkBindImageMemoryInfo *pBindInfos);

VkResult wsi_GetSwapchainStatusKHR(VkDevice device, VkSwapchainKHR swapchain);

} // namespace mali_wrapper
