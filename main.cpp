#include "VulkanRenderer.h"
#include "VulkanSwapchain.h"

#include "Window.h"

#include <stdio.h>
#include <math.h>

// wraps value to [0, K) exclusive
inline float Mod(float a, float k)
{
    return a - floorf(a/k)*k;
}

inline float SmoothPoly3(float t)
{
    return t*t*(3 - 2*t);
}

struct App {
    // The swapchains present mode = (immediatePresentation ? VK_PRESENT_MODE_IMMEDIATE_KHR : VK_PRESENT_MODE_FIFO_KHR);
    // FIFO should be vsync with syncInterval=1
    bool bImmediatePresentation = false;
    bool bDirtyVsync = false;
    // AKA "iconic" glfw has a callback for this as well as glfwGetWindowAttrib(window, GLFW_ICONIFIED)
    // The window class could just track this, to be more compatible with glfw which I should just probably use.
    bool isWindowIconic = false;
    VkExtent2D windowSize = { };
};

#define PERFRAME_CAPACITY 2

struct PerframeObjects {
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    VkFence fence;
    VkSemaphore swapchainImageAcquireSema;
    VkSemaphore swapchainImageReleaseSema;
};

App app;
Window window;

/*  NOTE: A WM_SIZE with wParam=SIZE_MINIMIZED
    passes 0 for both width and height, but a swapchain cannot
    be created with this extent.
*/
static void OnResizeClient(void *, int w, int h, bool isIconic)
{
    printf("%s: { w, h }: { %d, %d }, isIconic: %d\n", __FUNCTION__, w, h, int(isIconic));
    app.isWindowIconic = isIconic;
    app.windowSize = { uint32_t(w), uint32_t(h) };
}

static void OnVirtualKey(void *, uint32_t vkey, virtual_key_action action)
{
    if (vkey == WINDOW_VKEY_ESCAPE) {
        Window_SetShouldClose(window);
        return;
    }

    if (action == virtual_key_action::press) {
        switch (vkey) {
        /* NOTE: Must use capital letters in cases. */
        case 'V': {
            app.bDirtyVsync = true;
            app.bImmediatePresentation ^= 1;
        } break;
        } // end switch
    }
}

static void OnPaint(void *, int, int, int, int)
{
    puts(__FUNCTION__);
}

int main(int argc, char **argv)
{
    /*  Starting with VK_PRESENT_MODE_IMMEDIATE_KHR and useFences==true seems better than the vkDeviceWaitIdle.
        However, if I recreate the swapchain once (either resizing or changing vsync), then the times using fences
        look like vsync is always on??? This doesn't occur when using vkDeviceWaitIdle instead of fences.
     */
    bool useFences = false;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        for (const char *p = arg; *p; ++p) {
            if ((*p | 32u) == 'i') app.bImmediatePresentation = true;
            if ((*p | 32u) == 'f') useFences = true;
        }
    }

    printf("useFences: %d\n", int(useFences));

    VulkanRenderer vkr;
    Swapchain sc;
    int mainReturnCode = WindowWin32_Create(&window, 640, 480, "vk_win32_window",
                                            WindowClassFlag_HorizontalRedraw | WindowClassFlag_VerticalRedraw);
    if (mainReturnCode != 0) {
        puts("Failed to create window.");
        return mainReturnCode;
    }
    if (VkResult err = VKR_InitInstanceOnly(vkr)) {
        mainReturnCode = int(err);
        goto L_destroy_vkcore;
    }
    VK_CHECK(Swapchain_CreateSurfaceOnly(sc, vkr.instance, window.nativeHandle));
    if (VkResult err = VKR_PostInstanceConstruct(vkr, sc.surface)) {
        mainReturnCode = int(err);
        goto L_destroy_surface_and_swapchain;
    }
    VK_CHECK(Swapchain_InitParams(sc, vkr.physicalDevice, VK_IMAGE_USAGE_TRANSFER_DST_BIT)); // lets try vkCmdClearColorImage first.

    window.user_ptr = nullptr; // app is global for now
    window.user_cb = {
        OnResizeClient,
        OnVirtualKey,
        OnPaint
    };

    Window_Show(window);

    {
        static const VkSemaphoreCreateInfo BinarySemaCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        static const VkFenceCreateInfo SignaledFenceCreateInfo = {
            VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT
        };

        PerframeObjects perframe[PERFRAME_CAPACITY];

        for (PerframeObjects& pf : perframe) {
            // Consider VK_COMMAND_POOL_CREATE_TRANSIENT_BIT ?
            pf.commandPool = VKH_CreateCommandPool(vkr.device, 0, vkr.families.universal);
            pf.commandBuffer = VKH_AllocateCommandBuffer(vkr.device, pf.commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY);

            vkCreateFence(vkr.device, &SignaledFenceCreateInfo, nullptr, &pf.fence);

            vkCreateSemaphore(vkr.device, &BinarySemaCreateInfo, nullptr, &pf.swapchainImageAcquireSema);
            vkCreateSemaphore(vkr.device, &BinarySemaCreateInfo, nullptr, &pf.swapchainImageReleaseSema);
        }

        int64_t const TicksPerSecI64 = OS_TicksPerSecond();
        float const SecsPerTickF32 = 1.0f / float(TicksPerSecI64);
        os_tick_t const AppBeginTicks = OS_GetTicks();

        os_tick_t lastTitleTicks = 0;
        float frameDurationAvgSecs = 0.0;

        /* Say conservatively render 512 (2^9) frames per second. That rate will take 2^23 seconds to overflow a uint32_t.
         * (2^23 secs) / (60*60*24 secs/day) ~=  97 days, that should be fine.
         */
        for (uint32_t frameCounter = -1;;) {

            Window_DispatchMessagesNonblocking();
            if (Window_ShouldClose(window)) {
                break;
            }

            if (app.isWindowIconic) {
                OS_SleepMS(200);
                continue;
            } else {
                if (app.windowSize.width == 0 || app.windowSize.height == 0) {
                    puts("zero window area and not iconic");
                    ASSERT(0);
                    break;
                }
            }
            ++frameCounter; // starts at -1

            if (app.windowSize != sc.lastCreatedExtent || app.bDirtyVsync) {
                app.bDirtyVsync = false;
                VkSwapchainKHR oldSwapchain = sc.swapchain;
                VkPresentModeKHR presentMode = app.bImmediatePresentation ?
                                               VK_PRESENT_MODE_IMMEDIATE_KHR : VK_PRESENT_MODE_FIFO_KHR;
                VkResult createSwapcainRes = Swapchain_Create(sc, vkr.physicalDevice, vkr.device, app.windowSize, presentMode);
                VK_CHECK(createSwapcainRes);
                if (oldSwapchain) {
                    vkDeviceWaitIdle(vkr.device);
                    vkDestroySwapchainKHR(vkr.device, oldSwapchain, nullptr);
                }
            }

            os_tick_t const updateBeginTicks = OS_GetTicks();
            /* Do something that pulses between cyan and yellow. IDK if sRGB is applied here if the image fomrat is SRGB. */
            float elapsedSecs = float(updateBeginTicks - AppBeginTicks) * SecsPerTickF32;
            float t = Mod(elapsedSecs, 2.0f);
            t = t < 1.0f ? t : 2.0f - t;
            t = SmoothPoly3(t);
            VkClearColorValue color = { t, 1, 1-t, 1 };

            unsigned const pfi = frameCounter % PERFRAME_CAPACITY;

            if (useFences) {
                VkResult const waitFenceResult = vkWaitForFences(vkr.device, 1, &perframe[pfi].fence, true, uint64_t(-1));
                if (waitFenceResult != VK_SUCCESS) {
                    ASSERT(0);
                    break;
                }
                vkResetFences(vkr.device, 1, &perframe[pfi].fence);
            }
            else {
                VK_CHECK(vkDeviceWaitIdle(vkr.device));
            }

            /*  The sync objects aren't waited on here, they are signaled when the returned imageIndex is ready.
                wait operation on binary semaphore resets it to unsignaled.
                This call is blocking, so it may be best to call it as late as possible.
            */
            uint32_t imageIndex;
            VkResult const acquireImageResult =
                vkAcquireNextImageKHR(vkr.device, sc.swapchain, uint64_t(-1),
                                      perframe[pfi].swapchainImageAcquireSema, nullptr, &imageIndex);
            if (acquireImageResult != VK_SUCCESS) {
                /*  Since the swapchain is resized before this when the window area changes and is nonzero,
                    and an infinite timeout is used, I don't think any return code here would be non-serious.
                */
                printf("vkAcquireNextImageKHR returned %u\n", acquireImageResult);
                break;
            }

            VkCommandPool commandPool = perframe[pfi].commandPool;
            VK_CHECK(vkResetCommandPool(vkr.device, commandPool, 0));
            VkCommandBuffer commandBuffer = perframe[pfi].commandBuffer;

            VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

            /* change layout to DST_OPTIMAL */
            {
                VkImageMemoryBarrier beforeColorClearImageBarrier = {
                    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    nullptr,
                    0, // srcAccessMask
                    0, // dstAccessMask
                    VK_IMAGE_LAYOUT_UNDEFINED, // oldLayout, UNDEFINED is always allowed and means don't care about its contents.
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, // newLayout
                    vkr.families.universal, // srcQueueFamilyIndex, this could be a seperate family in some cases.
                    vkr.families.universal, // dstQueueFamilyIndex
                    sc.images[imageIndex],
                    FULL_IMAGE_RANGE_COLOR
                };
                /* IDK what src stage should be, the image should not be in use before submitting this command buffer.
                   TOP_OF_PIPE_BIT is generally the fastest thing.
                 */
                vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                     0, nullptr, 0, nullptr, 1, &beforeColorClearImageBarrier);
            }

            VkImageSubresourceRange clearRange = FULL_IMAGE_RANGE_COLOR;
            vkCmdClearColorImage(commandBuffer, sc.images[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &clearRange);

            /* Wait for execution of STAGE_TRANSFER_BIT and change layout from DST_OPTIMAL -> PRESENT_SRC_KHR */
            {
                VkImageMemoryBarrier beforePresentBarrier = {
                    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    nullptr,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    0,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    vkr.families.universal,
                    vkr.families.universal, // this could be a seperate family in some cases.
                    sc.images[imageIndex],
                    FULL_IMAGE_RANGE_COLOR
                };
                /* Not sure about dest pipeline stage. */
                vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
                                     0, nullptr, 0, nullptr, 1, &beforePresentBarrier);
            }
            VK_CHECK(vkEndCommandBuffer(commandBuffer));

            /* Wait on the semaphore to be signaled before executing this stage: */
            VkPipelineStageFlags waitDstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;

            VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = &perframe[pfi].swapchainImageAcquireSema;
            submitInfo.pWaitDstStageMask = &waitDstStageMask;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &perframe[pfi].swapchainImageReleaseSema;

            vkQueueSubmit(vkr.universalQueue0, 1, &submitInfo, useFences ? perframe[pfi].fence : nullptr);

            VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
            presentInfo.waitSemaphoreCount = 1;
            presentInfo.pWaitSemaphores = &perframe[pfi].swapchainImageReleaseSema;
            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains = &sc.swapchain;
            presentInfo.pImageIndices = &imageIndex;

            VK_CHECK(vkQueuePresentKHR(vkr.universalQueue0, &presentInfo));

            os_tick_t nowTicks = OS_GetTicks();
            float const K = 16.0f;
            float const thisDurationSecs = (nowTicks - updateBeginTicks) * SecsPerTickF32;
            frameDurationAvgSecs = frameDurationAvgSecs * ((K-1) / K) + thisDurationSecs * (1 / K);
            /* Update the title roughly at second intervals: */
            if (nowTicks - lastTitleTicks > TicksPerSecI64) {
                lastTitleTicks = nowTicks;
                char buf[120];
                sprintf(buf, "vsync: %c, ms: %f", unsigned(app.bImmediatePresentation)^'1', frameDurationAvgSecs * 1000);
                Window_SetTitle(window, buf);
            }
        } // end main loop

        vkDeviceWaitIdle(vkr.device);
        for (PerframeObjects& pf : perframe) {
            vkDestroyCommandPool(vkr.device, pf.commandPool, nullptr);

            vkDestroyFence(vkr.device, pf.fence, nullptr);

            vkDestroySemaphore(vkr.device, pf.swapchainImageAcquireSema, nullptr);
            vkDestroySemaphore(vkr.device, pf.swapchainImageReleaseSema, nullptr);
        }
    }

    vkDeviceWaitIdle(vkr.device);
L_destroy_surface_and_swapchain:
    Swapchain_DestroySwapchainAndSurface(sc, vkr.instance, vkr.device);
L_destroy_vkcore:
    VKR_Destruct(vkr);
    WindowWin32_Destroy(window);
    return mainReturnCode;
}
