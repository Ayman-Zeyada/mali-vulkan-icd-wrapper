/*
 * Copyright (c) 2018-2019, 2021-2022 Arm Limited.
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
 * @file surface_api.hpp
 *
 * @brief Contains the internal WSI surface implementation functions.
 */
#pragma once

#include <vulkan/vulkan.h>

namespace mali_wrapper {

// Internal WSI surface API for direct integration
VkResult wsi_GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                                    VkSurfaceCapabilitiesKHR *pSurfaceCapabilities);

VkResult wsi_GetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice,
                                                     const VkPhysicalDeviceSurfaceInfo2KHR *pSurfaceInfo,
                                                     VkSurfaceCapabilities2KHR *pSurfaceCapabilities);

VkResult wsi_GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                               uint32_t *pSurfaceFormatCount,
                                               VkSurfaceFormatKHR *pSurfaceFormats);

VkResult wsi_GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice,
                                                const VkPhysicalDeviceSurfaceInfo2KHR *pSurfaceInfo,
                                                uint32_t *pSurfaceFormatCount,
                                                VkSurfaceFormat2KHR *pSurfaceFormats);

VkResult wsi_GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                                    uint32_t *pPresentModeCount,
                                                    VkPresentModeKHR *pPresentModes);

VkResult wsi_GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
                                               VkSurfaceKHR surface, VkBool32 *pSupported);

void wsi_DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface,
                          const VkAllocationCallbacks *pAllocator);

// Surface creation functions
VkResult wsi_CreateXcbSurfaceKHR(VkInstance instance, const VkXcbSurfaceCreateInfoKHR* pCreateInfo,
                                const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface);

VkResult wsi_CreateXlibSurfaceKHR(VkInstance instance, const VkXlibSurfaceCreateInfoKHR* pCreateInfo,
                                 const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface);

VkResult wsi_CreateWaylandSurfaceKHR(VkInstance instance, const VkWaylandSurfaceCreateInfoKHR* pCreateInfo,
                                    const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface);

VkResult wsi_CreateHeadlessSurfaceEXT(VkInstance instance, const VkHeadlessSurfaceCreateInfoEXT* pCreateInfo,
                                     const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface);

} // namespace mali_wrapper
