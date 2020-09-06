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

struct vec2f { float x, y; };

struct vec4f {
    union {
        struct {
            float x, y, z, w;
        };
        struct {
            vec2f xy, zw;
        };
    };
};

/*
    Returns the cosine and sine using where [0, 1) revolutions maps to [0, 2pi) radians.
*/
inline vec2f cos_sin_tau(float revolutions)
{
    constexpr float Tau = float(3.14159265358979323846 * 2);
    float radians = revolutions * Tau;
    return {
        cosf(radians),
        sinf(radians)
    };
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

struct SwapchainRenderables {
    VkImageView view;
    VkFramebuffer framebuffer; // no DSV or any resolve attatchments.
};

static void
DestroySwapchainRenderables(VkDevice device, const SwapchainRenderables *a, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        vkDestroyFramebuffer(device, a[i].framebuffer, nullptr);
        vkDestroyImageView(device, a[i].view, nullptr);
    }
}

// The VkRenderPass only has to be a compatible VkRenderPass
static void
CreateSwapchainRenderables(VkDevice device, SwapchainRenderables *a, const Swapchain& sc, VkRenderPass renderPass)
{
    VkImageViewCreateInfo viewCreateInfo = {
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        nullptr,
        0, // VkImageViewCreateFlags
        nullptr, // VkImage, set in loop
        VK_IMAGE_VIEW_TYPE_2D,
        sc.format,
        COMPONENT_MAPPING_IDENTITY,
        FULL_IMAGE_RANGE_COLOR
    };

    VkFramebufferCreateInfo fbCreateInfo = {
        VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        nullptr,
        0, // VkFramebufferCreateFlags
        renderPass,
        1, // uint32_t attachmentCount, set in loop
        nullptr, // const VkImageView* pAttachments;
        sc.lastCreatedExtent.width, sc.lastCreatedExtent.height, 1 // uint32_t width, height, layers;
    };

    for (unsigned i = 0; i < sc.imageCount; ++i) {
        viewCreateInfo.image = sc.images[i];
        vkCreateImageView(device, &viewCreateInfo, nullptr, &a[i].view);
        fbCreateInfo.pAttachments = &a[i].view;
        vkCreateFramebuffer(device, &fbCreateInfo, nullptr, &a[i].framebuffer);
    }
}


static VkRenderPass
CreateRenderPass(VkDevice device, VkFormat color0_format)
{
    const VkAttachmentDescription color0_desc = {
        0, // VkAttachmentDescriptionFlags flags;
        color0_format, // VkFormat format;
        VK_SAMPLE_COUNT_1_BIT, // VkSampleCountFlagBits samples;
        VK_ATTACHMENT_LOAD_OP_CLEAR, // VkAttachmentLoadOp loadOp;
        VK_ATTACHMENT_STORE_OP_STORE, // VkAttachmentStoreOp storeOp;
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, //VkAttachmentLoadOp stencilLoadOp;
        VK_ATTACHMENT_STORE_OP_DONT_CARE, // VkAttachmentStoreOp stencilStoreOp;
        VK_IMAGE_LAYOUT_UNDEFINED, // VkImageLayout initialLayout, the layout before vkBeginRenderPass?
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR // VkImageLayout finalLayout, layout auto-changes to this after vkEndRenderPass?
    };

    const VkAttachmentReference color0_ref = {
        0, // attatchment index
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL // layout auto-changes to this when this subpass begins?
    };

    const VkSubpassDescription subpassDesc = {
        0, // VkSubpassDescriptionFlags flags;
        VK_PIPELINE_BIND_POINT_GRAPHICS, // VkPipelineBindPoint pipelineBindPoint;
        0, // uint32_t inputAttachmentCount;
        nullptr, // const VkAttachmentReference* pInputAttachments;
        1, // num color refs
        &color0_ref,
        nullptr, // const VkAttachmentReference* pResolveAttachments;
        nullptr, // const VkAttachmentReference* pDepthStencilAttachment;
        0, //  preserveAttachmentCount;
        nullptr // const uint32_t* pPreserveAttachments, why isnt this and the above parameter just a bitset?
    };

    const VkRenderPassCreateInfo rpCreateInfo = {
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        nullptr,
        0, // VkRenderPassCreateFlags flags;
        1, //uint32_t attachmentCount;
        &color0_desc, // const VkAttachmentDescription* pAttachments;
        1, // uint32_t subpassCount;
        &subpassDesc, // const VkSubpassDescription* pSubpasses;
        0, // uint32_t dependencyCount,  explicit deps
        nullptr // const VkSubpassDependency* pDependencies, explicit deps
    };

    VkRenderPass rp = 0;
    VK_CHECK(vkCreateRenderPass(device, &rpCreateInfo, nullptr, &rp));
    return rp;
}

static VkPipeline
CreatePipeline(VkDevice device, VkPipelineCache cache, VkPipelineLayout psoLayout,
               VkRenderPass renderPass, uint32_t subpass,
               VkShaderModule vs, VkShaderModule fs)
{
    VkPipelineShaderStageCreateInfo stages[2];
    stages[1] = stages[0] = {
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        nullptr,
        0, // VkPipelineShaderStageCreateFlags flags;
        VkShaderStageFlagBits(0),
        nullptr, // VkShaderModule module;
        "main", // const char* pName;
        nullptr // const VkSpecializationInfo* pSpecializationInfo;
    };

    stages[0].module = vs;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;

    stages[1].module = fs;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;

    // no attributes
    VkPipelineVertexInputStateCreateInfo vertex_input = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

    // Specify we will use triangle lists to draw geometry.
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        nullptr,
        0, // flags
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        false // primitive restart enabled.
    };

    // Specify rasterization state.
    VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    raster.cullMode  = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    // Our attachment will write to all color channels, but no blending is enabled.
    VkPipelineColorBlendAttachmentState blend_attachment = {
        false // blend enabled
        // ...
    };
    blend_attachment.colorWriteMask = 0xf;

    VkPipelineColorBlendStateCreateInfo blend = {
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        nullptr,
        0, // flags
        false, // logicOpEnable;
        VkLogicOp(0),
        1, // attachmentCount;
        &blend_attachment,
        {0.0f, 0.0f, 0.0f, 0.0f} // float blendConstants[4], can be dynamic state.
    };

    // We will have one viewport and scissor box.
    // the values for the rects are set dynamically.
    const VkPipelineViewportStateCreateInfo viewport = {
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        nullptr,
        0, // flags
        1, nullptr, // viewport count and values
        1, nullptr // scissor count and values
    };

    // Disable all depth testing.
    VkPipelineDepthStencilStateCreateInfo depth_stencil ={ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };

    // No multisampling.
    VkPipelineMultisampleStateCreateInfo multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Specify that these states will be dynamic, i.e. not part of pipeline state object.
    const VkDynamicState dynamics[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    VkPipelineDynamicStateCreateInfo dynamic = {
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0,
        lengthof(dynamics), dynamics
    };

    VkGraphicsPipelineCreateInfo psoInfo = {
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        nullptr,
        0, // flags
        lengthof(stages), // uint32_t stageCount;
        stages,
        &vertex_input,
        &input_assembly,
        nullptr,
        &viewport,
        &raster,
        &multisample,
        &depth_stencil,
        &blend,
        &dynamic,
        psoLayout,
        renderPass,
        subpass, // One can't use the same PSO in two different subpasses of a renderPass.
        nullptr, // VkPipeline basePipelineHandle;
        0, // int32_t basePipelineIndex;
    };

    VkPipeline pso = nullptr;
    VK_CHECK(vkCreateGraphicsPipelines(device, cache, 1, &psoInfo, nullptr, &pso));
    return pso;
}

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

const uint32_t * get_hello_vertex_spirv(size_t *pBytesize);
const uint32_t * get_hello_fragment_spirv(size_t *pBytesize);

struct PushConstants {
    vec4f m;
    vec4f translation; // .zw unused, pad out
};

/*
Okay, heres how I think push constants work:

    - Each VkPipelineLayout (not VkPipeline?) has its own blob of at least 128 bytes.
    - Unlike glUniform, push constants are NOT (I think) preserved, though not sure when they becomes invalid.
    - vkCmdPushConstants uploads to that by specifying absolute offsets.
    - Each shader stage can carve out a range from the blob. A range can also be
    used by multiple stages, but a stage can only access a single range.

*/

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
    VK_CHECK(Swapchain_InitParams(sc, vkr.physicalDevice, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT));

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

        SwapchainRenderables swapchainRenderables[4];

        VkRenderPass renderPass = CreateRenderPass(vkr.device, sc.format);

        const uint32_t *pCode;
        size_t codeByteSize;

        pCode = get_hello_vertex_spirv(&codeByteSize);
        VkShaderModule helloVS = VKH_CreateShaderModule(vkr.device, pCode, codeByteSize);
        pCode = get_hello_fragment_spirv(&codeByteSize);
        VkShaderModule helloFS = VKH_CreateShaderModule(vkr.device, pCode, codeByteSize);

        VkPushConstantRange vsPushConstantsRange = {
            VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants)
        };

        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
        };
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &vsPushConstantsRange;

        VkPipelineLayout pipelineLayout = nullptr;
        VK_CHECK(vkCreatePipelineLayout(vkr.device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

        VkPipeline pso = CreatePipeline(vkr.device, VkPipelineCache(nullptr), pipelineLayout, renderPass, 0,
            helloVS, helloFS);

        // ShaderModules can be destroyed after creating all pipelines that used them.

        vkDestroyShaderModule(vkr.device, helloFS, nullptr);
        vkDestroyShaderModule(vkr.device, helloVS, nullptr);

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

                unsigned oldNumImages = sc.imageCount;
                VkSwapchainKHR oldSwapchain = sc.swapchain;
                VkPresentModeKHR presentMode = app.bImmediatePresentation ?
                                               VK_PRESENT_MODE_IMMEDIATE_KHR : VK_PRESENT_MODE_FIFO_KHR;
                VkResult createSwapcainRes = Swapchain_Create(sc, vkr.physicalDevice, vkr.device, app.windowSize, presentMode);
                VK_CHECK(createSwapcainRes);
                if (sc.imageCount > lengthof(swapchainRenderables)) {
                    return 1;
                }

                if (oldSwapchain) {
                    vkDeviceWaitIdle(vkr.device);
                    DestroySwapchainRenderables(vkr.device, swapchainRenderables, oldNumImages);
                    vkDestroySwapchainKHR(vkr.device, oldSwapchain, nullptr);
                }

                CreateSwapchainRenderables(vkr.device, swapchainRenderables, sc, renderPass);
            }

            os_tick_t const updateBeginTicks = OS_GetTicks();
            float elapsedSecs = float(updateBeginTicks - AppBeginTicks) * SecsPerTickF32;
            float t = Mod(elapsedSecs*0.25, 2.0f);
            t = t < 1.0f ? t : 2.0f - t;
            t = SmoothPoly3(t);
            float c = t * 0.25f;
            VkClearValue clearValue = { VkClearColorValue{ c, c, c, 1 } };

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

            VkRect2D const renderRect = { {0, 0}, app.windowSize };

            VkRenderPassBeginInfo rp_begin = {
                VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr,
                renderPass,
                swapchainRenderables[imageIndex].framebuffer,
                renderRect,
                1, &clearValue // array of VkClearValue, indexed by attatchment indicies
            };
            // We will add draw commands in the same command buffer.
            vkCmdBeginRenderPass(commandBuffer, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

            // Bind the graphics pipeline.
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pso);

            VkViewport vp = { 0, 0, float(app.windowSize.width), float(app.windowSize.height), 0.0f, 1.0f };
            vkCmdSetViewport(commandBuffer, 0, 1, &vp); // first, count
            vkCmdSetScissor(commandBuffer, 0, 1, &renderRect); // first, count

            vec2f const U = cos_sin_tau(t);

            PushConstants pcData;
            pcData.m.xy = U;
            pcData.m.zw = { -U.y, U.x }; // CCW perpendicular == (UnitZ cross {U.x, U.y, 0}).xy
            pcData.translation = { t - 0.5f, 0 };
            vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof pcData, &pcData);
            vkCmdDraw(commandBuffer, 3, 1, 0, 0); // Draw three vertices with one instance.
            pcData.m.x *= -1;
            pcData.m.y *= -1;
            pcData.translation = { 0, t - 0.5f };
            vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof pcData, &pcData);
            vkCmdDraw(commandBuffer, 3, 1, 0, 0); // Draw three vertices with one instance.

            // Complete render pass, changes image layout to PRESENT_SRC
            vkCmdEndRenderPass(commandBuffer);

            VK_CHECK(vkEndCommandBuffer(commandBuffer));

            /* Wait on the semaphore to be signaled before executing this stage: */
            VkPipelineStageFlags waitDstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

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

        DestroySwapchainRenderables(vkr.device, swapchainRenderables, sc.imageCount);
        vkDestroyPipeline(vkr.device, pso, nullptr);
        vkDestroyPipelineLayout(vkr.device, pipelineLayout, nullptr);
        vkDestroyRenderPass(vkr.device, renderPass, nullptr);

        for (PerframeObjects& pf : perframe) {
            vkDestroyCommandPool(vkr.device, pf.commandPool, nullptr);

            vkDestroyFence(vkr.device, pf.fence, nullptr);

            vkDestroySemaphore(vkr.device, pf.swapchainImageAcquireSema, nullptr);
            vkDestroySemaphore(vkr.device, pf.swapchainImageReleaseSema, nullptr);
        }
    }

L_destroy_surface_and_swapchain:
    Swapchain_DestroySwapchainAndSurface(sc, vkr.instance, vkr.device);
L_destroy_vkcore:
    VKR_Destruct(vkr);
    WindowWin32_Destroy(window);
    return mainReturnCode;
}
