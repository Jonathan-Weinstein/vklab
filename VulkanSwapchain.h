#pragma once
#include "vk_procs.h"

struct Swapchain
{
    VkSwapchainKHR swapchain; // may change thorughout program.
    VkSurfaceKHR surface;
    VkFormat format;
    VkExtent2D lastCreatedExtent;
    uint32_t imageCount;
    VkImage images[8];
    VkImageUsageFlags imageUsageBits;
};

VkResult
Swapchain_CreateSurfaceOnly(Swapchain& sc, VkInstance instance, void *nativeWindowHandle);

VkResult
Swapchain_InitParams(Swapchain& sc, VkPhysicalDevice physicalDevice, VkImageUsageFlags imageUsageBits);

VkResult
Swapchain_Create(Swapchain& sc, VkPhysicalDevice physicalDevice, VkDevice device,
                 const VkExtent2D& windowSize, VkPresentModeKHR presentMode);

void
Swapchain_DestroySwapchainAndSurface(const Swapchain& sc, VkInstance instance, VkDevice device);

/*
 * Signed, as the conversion to float32 is more efficient (when the top u64 bit is not known by the compiler).
 * Note float32 should probably only be used for differences (not points in time) of tick ponts.
 */
typedef int64_t os_tick_t;

void OS_SleepMS(uint32_t ms);
int64_t OS_TicksPerSecond();
int64_t OS_GetTicks();
