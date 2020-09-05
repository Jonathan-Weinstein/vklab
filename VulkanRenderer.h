#pragma once

#include "vk_procs.h"

/* Basic volk usage:
    - Call `VkResult volkInitialize();`, should load at least vkCreateInstance.
    - Create a VkInstance.
    - Call `void volkLoadInstanceOnly(VkInstance instance);`
    - Make a VkDevice.
    - Call `void volkLoadDevice(VkDevice device);`
*/

#include "common.h"

#if is_debug
#define VK_CHECK(e) ASSERT((e) == VK_SUCCESS)
#else
#define VK_CHECK(e) ((void)(e))
#endif

struct QueueFamilies {
    // Graphics, transfer, and compute. Also assume presentation for now,
    // but amd/intel/nv support presentation in the graphics family.
    uint32_t universal;
};

struct VulkanRenderer
{
    VkDevice device;
    QueueFamilies families;
    VkQueue universalQueue0;

    VkInstance instance;
    VkPhysicalDevice physicalDevice;

#if is_debug
    VkDebugReportCallbackEXT debugReportCallback;
#endif
};

inline bool operator==(const VkExtent2D& a, const VkExtent2D& b)
{
    return ((a.width ^ b.width) | (a.height ^ b.height)) == 0;
}

inline bool operator!=(const VkExtent2D& a, const VkExtent2D& b)
{
    return !(a == b);
}



VkResult VKR_InitInstanceOnly(VulkanRenderer& r);
VkResult VKR_PostInstanceConstruct(VulkanRenderer& r, VkSurfaceKHR surface);
void VKR_Destruct(VulkanRenderer& r);


// VKH_ = VulkanHelper, simple wrapper functions.

VkCommandPool
VKH_CreateCommandPool(VkDevice device, VkCommandPoolCreateFlags flags, uint32_t family);

VkCommandBuffer
VKH_AllocateCommandBuffer(VkDevice device, VkCommandPool cmdPool, VkCommandBufferLevel level);


//{ VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS }
#define FULL_IMAGE_RANGE_COLOR VkImageSubresourceRange{ 1, 0, 0xffffffffu, 0, 0xffffffffu }
