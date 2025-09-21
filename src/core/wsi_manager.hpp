#pragma once

#include <vulkan/vulkan.h>
#include <cstddef>
#include <memory>
#include <unordered_map>

namespace mali_wrapper {

class instance_private_data;
class device_private_data;

void add_instance_reference(VkInstance instance);
void remove_instance_reference(VkInstance instance);
bool is_instance_valid(VkInstance instance);

/**
 * @brief WSI Manager - Direct integration of WSI functionality
 *
 * This class replaces the Vulkan layer interface and provides direct WSI
 * functionality integration into the Mali wrapper to eliminate device
 * handle corruption issues.
 */
class WSIManager {
public:
    WSIManager();
    ~WSIManager();

    VkResult initialize(VkInstance instance, VkPhysicalDevice physicalDevice);
    VkResult init_device(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice mali_device,
                         const char *const *enabled_extensions, size_t enabled_extension_count);
    void cleanup();

    void release_device(VkDevice device);
    void release_instance(VkInstance instance);

    // Surface API
    VkResult create_surface_xcb(VkInstance instance, const VkXcbSurfaceCreateInfoKHR* pCreateInfo,
                               const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface);
    VkResult create_surface_xlib(VkInstance instance, const VkXlibSurfaceCreateInfoKHR* pCreateInfo,
                                const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface);
    VkResult create_surface_wayland(VkInstance instance, const VkWaylandSurfaceCreateInfoKHR* pCreateInfo,
                                   const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface);
    VkResult create_surface_headless(VkInstance instance, const VkHeadlessSurfaceCreateInfoEXT* pCreateInfo,
                                    const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface);
    VkResult destroy_surface(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks* pAllocator);

    // Surface properties
    VkResult get_surface_support(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
                                VkSurfaceKHR surface, VkBool32* pSupported);
    VkResult get_surface_capabilities(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                     VkSurfaceCapabilitiesKHR* pSurfaceCapabilities);
    VkResult get_surface_capabilities2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo,
                                      VkSurfaceCapabilities2KHR* pSurfaceCapabilities);
    VkResult get_surface_formats(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                uint32_t* pSurfaceFormatCount, VkSurfaceFormatKHR* pSurfaceFormats);
    VkResult get_surface_formats2(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR* pSurfaceInfo,
                                 uint32_t* pSurfaceFormatCount, VkSurfaceFormat2KHR* pSurfaceFormats);
    VkResult get_surface_present_modes(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                      uint32_t* pPresentModeCount, VkPresentModeKHR* pPresentModes);
    VkBool32 get_wayland_presentation_support(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
                                             struct wl_display* display);

    // Swapchain API
    VkResult create_swapchain(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                             const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain);
    VkResult destroy_swapchain(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator);
    VkResult get_swapchain_images(VkDevice device, VkSwapchainKHR swapchain,
                                 uint32_t* pSwapchainImageCount, VkImage* pSwapchainImages);
    VkResult acquire_next_image(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
                               VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex);
    VkResult acquire_next_image2(VkDevice device, const VkAcquireNextImageInfoKHR* pAcquireInfo, uint32_t* pImageIndex);
    VkResult queue_present(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);
    VkResult get_swapchain_status(VkDevice device, VkSwapchainKHR swapchain);

    VkResult get_device_group_present_capabilities(VkDevice device, VkDeviceGroupPresentCapabilitiesKHR* pDeviceGroupPresentCapabilities);
    VkResult get_device_group_surface_present_modes(VkDevice device, VkSurfaceKHR surface, VkDeviceGroupPresentModeFlagsKHR* pModes);
    VkResult get_physical_device_present_rectangles(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                                   uint32_t* pRectCount, VkRect2D* pRects);

    bool is_wsi_function(const char* function_name);
    PFN_vkVoidFunction get_function_pointer(const char* function_name);


    instance_private_data* lookup_instance(VkInstance instance);
    instance_private_data* lookup_first_instance(); // For physical device lookups
    device_private_data* lookup_device(VkDevice device);
    device_private_data* lookup_first_device(); // For queue lookups

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

WSIManager& GetWSIManager();

} // namespace mali_wrapper
