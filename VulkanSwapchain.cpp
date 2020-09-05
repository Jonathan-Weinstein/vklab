/*
    Code to create a surface and then swapchain. Also dumped some OS specific utils here.
*/

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif

#ifndef VC_EXTRALEAN
    #define VC_EXTRALEAN
#endif

#include "VulkanSwapchain.h"
#include "VulkanRenderer.h"

#include "common.h"

#include <Windows.h>

#include <stdio.h>
#include <stdlib.h>

VkResult
Swapchain_CreateSurfaceOnly(Swapchain& sc, VkInstance instance, void *nativeWindowHandle)
{
    VkWin32SurfaceCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    createInfo.hinstance = GetModuleHandle(0);
    createInfo.hwnd = (HWND)nativeWindowHandle;

    sc.swapchain = nullptr;

    sc.surface = nullptr;
    VkResult res = vkCreateWin32SurfaceKHR(instance, &createInfo, 0, &sc.surface);
    if (res != VK_SUCCESS) {
        if (sc.surface) {
            vkDestroySurfaceKHR(instance, sc.surface, nullptr);
            sc.surface = nullptr;
        }
    }
    return res;
}

void
Swapchain_DestroySwapchainAndSurface(const Swapchain& sc, VkInstance instance, VkDevice device)
{
    if (sc.swapchain) {
        vkDestroySwapchainKHR(device, sc.swapchain, nullptr);
    }

    if (sc.surface) {
        vkDestroySurfaceKHR(instance, sc.surface, nullptr);
    }
}


//must retrieve old swapcahin first!
VkResult
Swapchain_Create(Swapchain& sc, VkPhysicalDevice physicalDevice, VkDevice device, const VkExtent2D& windowSize, VkPresentModeKHR presentMode)
{
    VkSwapchainCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };

    {
        VkSurfaceCapabilitiesKHR surfaceCaps;
        VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, sc.surface, &surfaceCaps));
        if (surfaceCaps.currentExtent != windowSize) {
            fputs("inconsistent sizes?", stderr);
            exit(1);
        }

        createInfo.compositeAlpha = (surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ?
                                        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        createInfo.minImageCount = Max<uint>(2u, surfaceCaps.minImageCount);
    }

    createInfo.surface = sc.surface;
    createInfo.imageFormat = sc.format;
    createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    createInfo.imageExtent = windowSize;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = sc.imageUsageBits;

    /*  Sharing mode is VK_SHARING_MODE_EXCLUSIVE (0), already zerod by = { } */
    // createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    // createInfo.queueFamilyIndexCount = 0; // ignored when exclusive
    // createInfo.pQueueFamilyIndices = nullptr; // ignored when exclusive

    createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    createInfo.presentMode = presentMode;

    /*  Applications should set this value to VK_TRUE if they do not expect to read back the content of presentable images
        before presenting them or after reacquiring them, and if their fragment shaders do not have any side effects that require
        them to run for all pixels in the presentable image.
    */
    createInfo.clipped = true;

    createInfo.oldSwapchain = sc.swapchain;

    VkSwapchainKHR newSwapchain;
    VkResult res = vkCreateSwapchainKHR(device, &createInfo, 0, &newSwapchain);
    if (res == 0) {
        sc.swapchain = newSwapchain;
        sc.lastCreatedExtent = createInfo.imageExtent;

        uint32_t n = 0;
        VK_CHECK(vkGetSwapchainImagesKHR(device, sc.swapchain, &n, nullptr));
        ASSERT(n >= 2u);
        ASSERT(n < lengthof(sc.images));
        VK_CHECK(vkGetSwapchainImagesKHR(device, sc.swapchain, &n, sc.images));

        sc.imageCount = n;
    }
    return res;
}

//must be called after the surface is created:
VkResult
Swapchain_InitParams(Swapchain& sc, VkPhysicalDevice physicalDevice, VkImageUsageFlags imageUsageBits)
{
    /* I guess one could want more than one, but I don't think I'll be doing that. */
    ASSERT(imageUsageBits == VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT ||
           imageUsageBits == VK_IMAGE_USAGE_TRANSFER_DST_BIT ||
           imageUsageBits == VK_IMAGE_USAGE_STORAGE_BIT);

    VkFormat format = VK_FORMAT_UNDEFINED;
    // get a surface format
    {
        VkSurfaceFormatKHR formats[200];
        uint32_t formatCount = lengthof(formats);
        VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, sc.surface, &formatCount, formats));
        ASSERT(formatCount);
        ASSERT(formatCount < lengthof(formats));

        if (formatCount == 1 && formats[0].format == VK_FORMAT_UNDEFINED) {
            format = VK_FORMAT_R8G8B8A8_UNORM;
        }
        else {
            for (uint32_t i = 0; i < formatCount; ++i) {
                /* NOTE: sRGB */
                if (formats[i].format == VK_FORMAT_R8G8B8A8_SRGB ||
                    formats[i].format == VK_FORMAT_B8G8R8A8_SRGB) {
                    format = formats[i].format;
                    ASSERT(formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
                }
            }
        }
    }
    printf("Using swapchain format %d, sRGB ? %c\n", format, '0'+(format == VK_FORMAT_R8G8B8A8_SRGB ||
                                                                  format == VK_FORMAT_B8G8R8A8_SRGB));

    sc.format = format;
    sc.imageUsageBits = imageUsageBits;

    sc.swapchain = nullptr;
    sc.lastCreatedExtent = {};

    VkPresentModeKHR presentModes[8]; // there should be less than this many VK_PRESENT_MODE_* values.
    uint32_t count = lengthof(presentModes);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, sc.surface, &count, presentModes);
    printf("Supported presentation modes: {");
    for (uint i = 0; ;) {
        printf(" %d", presentModes[i]);
        if (++i == count) { break; }
        putchar(',');
    }
    puts(" }");

    return VK_SUCCESS;
}

/*  Yeild the calling threads CPU time by the specified millisecond duration.
    The resolution of this is poor and the actual duration may be much longer.
*/
void OS_SleepMS(uint32_t ms)
{
    Sleep(ms);
}

int64_t OS_TicksPerSecond()
{
    LARGE_INTEGER li;
    QueryPerformanceFrequency(&li);
    return li.QuadPart;
}

int64_t OS_GetTicks()
{
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return li.QuadPart;
}


#if 0
float sq2f(int64_t q)
{
    return float(q);
}

float uq2f(uint64_t q)
{
    return float(q);
}

/* gcc 10.2 -O2
sq2f(long):
        pxor    xmm0, xmm0 ; clang doesn't do this zero
        cvtsi2ss        xmm0, rdi
        ret
uq2f(unsigned long):
        test    rdi, rdi
        js      .L4
        pxor    xmm0, xmm0
        cvtsi2ss        xmm0, rdi
        ret
.L4:
        mov     rax, rdi
        and     edi, 1
        pxor    xmm0, xmm0 ; clang doesn't do this zero
        shr     rax
        or      rax, rdi
        cvtsi2ss        xmm0, rax
        addss   xmm0, xmm0
        ret
*/
#endif

