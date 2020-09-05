#include "VulkanRenderer.h"

#include <stdio.h>
#include <stdlib.h>

#if is_debug
static VkBool32 VKAPI_CALL
DebugReportCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT/*objectType*/, uint64_t/*object*/,
                    size_t/*location*/, int32_t/*messageCode*/,
                    const char* /*pLayerPrefix*/, const char* pMessage, void* /*pUserData*/)
{
    // This silences warnings like "For optimal performance image layout should be VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL instead of GENERAL."
    // We'll assume other performance warnings are also not useful.
    //if (flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) return VK_FALSE;

    const char* type =
        (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
        ? "ERROR"
        : (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT)
            ? "WARNING"
            : "INFO";

    printf("%s: %s\n", type, pMessage);

    if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
        assert(!"Validation error encountered!");

    return VK_FALSE;
}

static void
RegisterDebugReportCallback(VkInstance instance, VkDebugReportCallbackEXT *pCallback)
{
    VkDebugReportCallbackCreateInfoEXT createInfo = { VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT };
    createInfo.flags = VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_ERROR_BIT_EXT;
    createInfo.pfnCallback = DebugReportCallback;

    *pCallback = 0;
    VK_CHECK(vkCreateDebugReportCallbackEXT(instance, &createInfo, 0, pCallback));
}
#endif

VkResult
VKR_InitInstanceOnly(VulkanRenderer& vkr)
{
    vkr = { }; // Zero POD struct
    vkr.families = { VK_QUEUE_FAMILY_IGNORED };

#ifndef VK_VERSION_1_2
    #error "defines/headers not configured well"
#endif // !VK_VERSION_1_2

    if (volkInitialize() != VK_SUCCESS || volkGetInstanceVersion() < VK_API_VERSION_1_2) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo createInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    createInfo.pApplicationInfo = &appInfo;

#if is_debug
    const char* debugLayers[] =
    {
        "VK_LAYER_KHRONOS_validation"
    };

    createInfo.ppEnabledLayerNames = debugLayers;
    createInfo.enabledLayerCount = lengthof(debugLayers);

    printf("%s enabled layer\n", debugLayers[0]);
#endif

    const char* extensions[] =
    {
        VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef _WIN32
        "VK_KHR_win32_surface",//VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
#if is_debug
        VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
#endif
    };

    createInfo.ppEnabledExtensionNames = extensions;
    createInfo.enabledExtensionCount = lengthof(extensions);

    VkResult res = vkCreateInstance(&createInfo, nullptr, &vkr.instance);
    if (res == VK_SUCCESS) {
        volkLoadInstanceOnly(vkr.instance);
    #if is_debug
        RegisterDebugReportCallback(vkr.instance, &vkr.debugReportCallback);
    #endif
    }
    return res;
}

static const char *
GetDeviceTypeString(VkPhysicalDeviceType type)
{
    static const char *const DeviceTypeNames[] = {
        "other",//VK_PHYSICAL_DEVICE_TYPE_OTHER = 0,
        "integrated",//VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU = 1,
        "discrete",//VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU = 2,
        "virtual",//VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU = 3,
        "CPU"//VK_PHYSICAL_DEVICE_TYPE_CPU = 4
    };
    return size_t(type) < lengthof(DeviceTypeNames) ? DeviceTypeNames[type] : "???";
}

static VkPhysicalDevice
PickPhysicalDeviceAndFindFamilies(VkSurfaceKHR surface,
                                  const VkPhysicalDevice *physicalDevices, uint32_t physicalDeviceCount,
                                  QueueFamilies *families)
{
    VkPhysicalDevice selected = 0;
    VkPhysicalDeviceProperties props;
    VkQueueFamilyProperties familyProps[32];

    ASSERT(families->universal == VK_QUEUE_FAMILY_IGNORED);

    uint32_t gpuIndex = 0;
    for (; gpuIndex < physicalDeviceCount; ++gpuIndex) {
        VkPhysicalDevice const physdev = physicalDevices[gpuIndex];
        vkGetPhysicalDeviceProperties(physdev, &props);
        printf("GPU[%d]: (%s), type=%s\n", gpuIndex, props.deviceName, GetDeviceTypeString(props.deviceType));
        if (props.apiVersion < VK_API_VERSION_1_2) {
            printf("GPU%d apiVersion < VK_API_VERSION_1_2\n", gpuIndex);
            continue;
        }
        int32_t universalFam = -1;

        uint32_t numFamilies = lengthof(familyProps);
        vkGetPhysicalDeviceQueueFamilyProperties(physdev, &numFamilies, familyProps);
        ASSERT(numFamilies < 32u);
        ASSERT(numFamilies);
        printf("number of queue families: %d\n", numFamilies);
        for (uint32_t fam = 0; fam < numFamilies; ++fam) {
            printf("family[%u].queueFlags = 0x%X\n", fam, familyProps[fam].queueFlags);
            constexpr VkFlags universalFlags =
                VK_QUEUE_GRAPHICS_BIT |
                VK_QUEUE_COMPUTE_BIT |
                VK_QUEUE_TRANSFER_BIT;
            if ((familyProps[fam].queueFlags & universalFlags) == universalFlags) {
                VkBool32 supportsPresentation = false;
                vkGetPhysicalDeviceSurfaceSupportKHR(physdev, fam, surface, &supportsPresentation);
                if (universalFam < 0) {
                    universalFam = fam;
                }
            }
        }

        if (int32_t(universalFam) >= 0) {
            selected = physdev;
            families->universal = universalFam;
            break;
        }
    }

    if (selected) {
        printf("Selected GPU[%d] (%s)\n", gpuIndex, props.deviceName);
        ASSERT(int32_t(families->universal) >= 0);
        return selected;
    } else {
        printf("ERROR: No suitable GPU found\n");
        return nullptr;
    }
}


static VkDevice
CreateDevice(VkPhysicalDevice physicalDevice, const QueueFamilies& families)
{
    const float queuePriorities[] = { 1.0f };

    VkDeviceQueueCreateInfo queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    queueInfo.queueFamilyIndex = families.universal;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = queuePriorities;

    //push descriptors?
    const char *const extensions[] =
    {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    //if (pushDescriptorsSupported) extensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);

    //if (checkpointsSupported) extensions.push_back(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);

    /* Difference from VkPhysicalDeviceFeatures (the .features member) is that this has sType/pNext: */
    VkPhysicalDeviceFeatures2 features2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    // features2.features.robustBufferAccess = true; // this is not very strict, robustBufferAccess2 has more guarantees.
    // features2.features.shaderInt16 = true;
    // features2.features.fillModeNonSolid = true; // needed for wireframe triangles
    VkPhysicalDeviceVulkan12Features features1_2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    // features1_2.timelineSemaphore = true;
    // features1_2.imagelessFramebuffer = true;
    // features1_2.scalarBlockLayout = true;
    // features1_2.shaderFloat16 = true;
    // features1_2.shaderInt8 = true;
    // features1_2.storagePushConstant8 = true;

    VkDeviceCreateInfo createInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueInfo;

    createInfo.ppEnabledExtensionNames = extensions;
    createInfo.enabledExtensionCount = lengthof(extensions);

    features2.pNext = &features1_2;
    createInfo.pNext = &features2;

    VkDevice device = 0;
    VK_CHECK(vkCreateDevice(physicalDevice, &createInfo, 0, &device));
    return device;
}


VkResult
VKR_PostInstanceConstruct(VulkanRenderer& vkr, VkSurfaceKHR surface)
{

    {
        VkPhysicalDevice physicalDevices[8];
        uint32_t physicalDeviceCount = lengthof(physicalDevices);
        VK_CHECK(vkEnumeratePhysicalDevices(vkr.instance, &physicalDeviceCount, physicalDevices));

        vkr.physicalDevice = PickPhysicalDeviceAndFindFamilies(surface,
                                                               physicalDevices, physicalDeviceCount,
                                                               &vkr.families);
    }
    vkr.device = CreateDevice(vkr.physicalDevice, vkr.families);

    if (vkr.device) {
        volkLoadDevice(vkr.device);
        /* There may be multiple queues in this family, get only one: */
        vkGetDeviceQueue(vkr.device, vkr.families.universal, 0, &vkr.universalQueue0);
    }
    else {
        VKR_Destruct(vkr);
        vkr.instance = nullptr;
        vkr.device = nullptr;
    }

    return VK_SUCCESS;
}

void
VKR_Destruct(VulkanRenderer& vkr)
{
    // VkQueue has no no explicit destroy.

    VkDevice dev = vkr.device;
    if (dev) {
        vkDeviceWaitIdle(dev);
        vkDestroyDevice(dev, nullptr);
    }
    // VkPhysicalDevice has no no excplicit destroy.

    if (vkr.instance) {
    #if is_debug
        vkDestroyDebugReportCallbackEXT(vkr.instance, vkr.debugReportCallback, nullptr);
    #endif
        vkDestroyInstance(vkr.instance, nullptr);
    }
}

VkCommandPool
VKH_CreateCommandPool(VkDevice device, VkCommandPoolCreateFlags flags, uint32_t family)
{
    VkCommandPoolCreateInfo poolInfo = {
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        nullptr,
        flags,
        family
    };
    VkCommandPool cmdPool = 0;
    VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &cmdPool));
    return cmdPool;
}

VkCommandBuffer
VKH_AllocateCommandBuffer(VkDevice device, VkCommandPool cmdPool, VkCommandBufferLevel level)
{
    VkCommandBufferAllocateInfo bufInfo = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        nullptr,
        cmdPool,
        level,
        1 // commandBufferCount, can pass an array of VkCommandBuffer
    };
    VkCommandBuffer cmdBuffer = 0;
    VK_CHECK(vkAllocateCommandBuffers(device, &bufInfo, &cmdBuffer));
    return cmdBuffer;
}
