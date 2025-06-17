// Dear ImGui: standalone example application for SDL3 + Vulkan

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

// Important note to the reader who wish to integrate imgui_impl_vulkan.cpp/.h in their own engine/app.
// - Common ImGui_ImplVulkan_XXX functions and structures are used to interface with imgui_impl_vulkan.cpp/.h.
//   You will use those if you want to use this rendering backend in your engine/app.
// - Helper ImGui_ImplVulkanH_XXX functions and structures are only used by this example (main.cpp) and by
//   the backend itself (imgui_impl_vulkan.cpp), but should PROBABLY NOT be used by your own engine/app code.
// Read comments in imgui_impl_vulkan.h.

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>

#include <stdio.h>  // printf, fprintf
#include <stdlib.h> // abort
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

// This example doesn't compile with Emscripten yet! Awaiting SDL3 support.
#ifdef __EMSCRIPTEN__
#include "../libs/emscripten/emscripten_mainloop_stub.h"
#endif

#include <openvr.h>

#include <sstream>
#include <fstream>
#include <vector>

// #define APP_USE_UNLIMITED_FRAME_RATE
#ifdef _DEBUG
#define APP_USE_VULKAN_DEBUG_REPORT
static VkDebugReportCallbackEXT g_DebugReport = VK_NULL_HANDLE;
#endif

// Data
static VkAllocationCallbacks* g_Allocator = nullptr;
static VkInstance g_Instance = VK_NULL_HANDLE;
static VkPhysicalDevice g_PhysicalDevice = VK_NULL_HANDLE;
static VkDevice g_Device = VK_NULL_HANDLE;
static uint32_t g_QueueFamily = (uint32_t)-1;
static VkQueue g_Queue = VK_NULL_HANDLE;
static VkPipelineCache g_PipelineCache = VK_NULL_HANDLE;
static VkDescriptorPool g_DescriptorPool = VK_NULL_HANDLE;

static ImGui_ImplVulkanH_Window g_MainWindowData;
static uint32_t g_MinImageCount = 2;
static bool g_SwapChainRebuild = false;
static vr::VROverlayHandle_t g_Overlayhandle = NULL;
static vr::VROverlayHandle_t g_OverlayThumbnailHandle = NULL;
static float g_RefreshRate = 60.0f; // default

static void check_vk_result(VkResult err) {
    if (err == VK_SUCCESS)
        return;
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
    if (err < 0)
        abort();
}

#ifdef APP_USE_VULKAN_DEBUG_REPORT
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_report(
    VkDebugReportFlagsEXT flags,
    VkDebugReportObjectTypeEXT objectType,
    uint64_t object,
    size_t location,
    int32_t messageCode,
    const char* pLayerPrefix,
    const char* pMessage,
    void* pUserData
) {
    (void)flags;
    (void)object;
    (void)location;
    (void)messageCode;
    (void)pUserData;
    (void)pLayerPrefix; // Unused arguments

    // Filter the specific message by substring match
    if (strstr(
            pMessage,
            "pCreateInfo->pNext<VkExternalMemoryImageCreateInfo>.handleTypes is 16 but the initialLayout is "
            "VK_IMAGE_LAYOUT_PREINITIALIZED"
        ) != NULL
        && objectType == VK_DEBUG_REPORT_OBJECT_TYPE_UNKNOWN_EXT) {
        // supress report from internal OpenVR behaviour where calling vr::VROverlay()->SetOverlayTexture
        // initializes VkExternalMemoryImageCreateInfo with VK_IMAGE_LAYOUT_PREINITIALIZED instead of VK_IMAGE_LAYOUT_UNDEFINED
        return VK_FALSE;
    }

    fprintf(stderr, "[vulkan] Debug report from ObjectType: %i\nMessage: %s\n\n", objectType, pMessage);
    return VK_FALSE;
}
#endif // APP_USE_VULKAN_DEBUG_REPORT

static bool IsExtensionAvailable(const ImVector<VkExtensionProperties>& properties, const char* extension) {
    for (const VkExtensionProperties& p : properties)
        if (strcmp(p.extensionName, extension) == 0)
            return true;
    return false;
}

static std::vector<std::string> requested_instance_extensions = {};
static std::vector<std::string> requested_device_extensions = {};

static void SetupVulkan() {
    VkResult err;
#ifdef IMGUI_IMPL_VULKAN_USE_VOLK
    volkInitialize();
#endif

    requested_instance_extensions.clear();
    requested_device_extensions.clear();

    // Create Vulkan Instance
    {
        VkInstanceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

        // Enumerate available extensions
        uint32_t properties_count;
        ImVector<VkExtensionProperties> properties;
        vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, nullptr);
        properties.resize(properties_count);
        err = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, properties.Data);
        check_vk_result(err);

        uint32_t bufCount = vr::VRCompositor()->GetVulkanInstanceExtensionsRequired(nullptr, 0);

        // we have some extensions to add!
        if (bufCount > 0) {
            std::vector<char> extensionBuffer(bufCount + 1); // +1 for null
            vr::VRCompositor()->GetVulkanInstanceExtensionsRequired(extensionBuffer.data(), bufCount);
            extensionBuffer[bufCount] = '\0'; // force null-termination

            std::string token;
            std::istringstream tokenStream(extensionBuffer.data());
            while (std::getline(tokenStream, token, ' ')) {
                if (IsExtensionAvailable(properties, token.data())) {
                    printf("%s Instance Extension asked by OpenVR was available\n", token.data());
                    requested_instance_extensions.push_back(token);
                } else {
                    printf("ERROR! %s Instance Extension asked by OpenVR was NOT available\n", token.data());
                    std::exit(EXIT_FAILURE); // abort!
                }
            }
        }

#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
            requested_instance_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
#endif

        // Enabling validation layers
#ifdef APP_USE_VULKAN_DEBUG_REPORT
        const char* layers[] = {"VK_LAYER_KHRONOS_validation"};
        create_info.enabledLayerCount = 1;
        create_info.ppEnabledLayerNames = layers;
        requested_instance_extensions.push_back("VK_EXT_debug_report");
#endif

        std::vector<const char*> enabled_extensions {};
        for (auto& extension : requested_instance_extensions) {
            printf("Registring instance extension %s\n", extension.data());
            enabled_extensions.push_back(extension.data());
        }

        // Create Vulkan Instance
        create_info.enabledExtensionCount = (uint32_t)enabled_extensions.size();
        create_info.ppEnabledExtensionNames = enabled_extensions.data();
        err = vkCreateInstance(&create_info, g_Allocator, &g_Instance);
        check_vk_result(err);
#ifdef IMGUI_IMPL_VULKAN_USE_VOLK
        volkLoadInstance(g_Instance);
#endif

        // Setup the debug report callback
#ifdef APP_USE_VULKAN_DEBUG_REPORT
        auto f_vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT
        )vkGetInstanceProcAddr(g_Instance, "vkCreateDebugReportCallbackEXT");
        IM_ASSERT(f_vkCreateDebugReportCallbackEXT != nullptr);
        VkDebugReportCallbackCreateInfoEXT debug_report_ci = {};
        debug_report_ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
        debug_report_ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT
                              | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
        debug_report_ci.pfnCallback = debug_report;
        debug_report_ci.pUserData = nullptr;
        err = f_vkCreateDebugReportCallbackEXT(g_Instance, &debug_report_ci, g_Allocator, &g_DebugReport);
        check_vk_result(err);
#endif
    }

    // Select Physical Device (GPU)
    g_PhysicalDevice = ImGui_ImplVulkanH_SelectPhysicalDevice(g_Instance);
    IM_ASSERT(g_PhysicalDevice != VK_NULL_HANDLE);

    // Select graphics queue family
    g_QueueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(g_PhysicalDevice);
    IM_ASSERT(g_QueueFamily != (uint32_t)-1);

    // Create Logical Device (with 1 queue)
    {
        requested_device_extensions.push_back("VK_KHR_swapchain");

        // Enumerate physical device extension
        uint32_t properties_count;
        ImVector<VkExtensionProperties> properties;
        vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr, &properties_count, nullptr);
        properties.resize(properties_count);
        vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr, &properties_count, properties.Data);

        uint32_t bufCount = vr::VRCompositor()->GetVulkanDeviceExtensionsRequired(g_PhysicalDevice, nullptr, 0);

        // we have some extensions to add!
        if (bufCount > 0) {
            std::vector<char> extensionBuffer(bufCount + 1); // +1 for null
            vr::VRCompositor()->GetVulkanDeviceExtensionsRequired(g_PhysicalDevice, extensionBuffer.data(), bufCount);
            extensionBuffer[bufCount] = '\0'; // force null-termination

            std::string token;
            std::istringstream tokenStream(extensionBuffer.data());
            while (std::getline(tokenStream, token, ' ')) {
                if (IsExtensionAvailable(properties, token.data())) {
                    printf("%s Device Extension asked by OpenVR was available\n", token.data());
                    requested_device_extensions.push_back(token);
                } else {
                    printf("ERROR! %s Device Extension asked by OpenVR was NOT available\n", token.data());
                    std::exit(EXIT_FAILURE); // abort!
                }
            }
        }

        std::vector<const char*> enabled_extensions{};
        for (auto& extension : requested_device_extensions) {
            printf("Registring device extension %s\n", extension.data());
            enabled_extensions.push_back(extension.data());
        }

#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
        if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
            enabled_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

        const float queue_priority[] = {1.0f};
        VkDeviceQueueCreateInfo queue_info[1] = {};
        queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info[0].queueFamilyIndex = g_QueueFamily;
        queue_info[0].queueCount = 1;
        queue_info[0].pQueuePriorities = queue_priority;
        VkDeviceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = sizeof(queue_info) / sizeof(queue_info[0]);
        create_info.pQueueCreateInfos = queue_info;
        create_info.enabledExtensionCount = (uint32_t)enabled_extensions.size();
        create_info.ppEnabledExtensionNames = enabled_extensions.data();
        err = vkCreateDevice(g_PhysicalDevice, &create_info, g_Allocator, &g_Device);
        check_vk_result(err);
        vkGetDeviceQueue(g_Device, g_QueueFamily, 0, &g_Queue);
    }

    // Create Descriptor Pool
    // If you wish to load e.g. additional textures you may need to alter pools sizes and maxSets.
    {
        VkDescriptorPoolSize pool_sizes[] = {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE},
        };
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 0;
        for (VkDescriptorPoolSize& pool_size : pool_sizes)
            pool_info.maxSets += pool_size.descriptorCount;
        pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;
        err = vkCreateDescriptorPool(g_Device, &pool_info, g_Allocator, &g_DescriptorPool);
        check_vk_result(err);
    }
}

// All the ImGui_ImplVulkanH_XXX structures/functions are optional helpers used by the demo.
// Your real engine/app may not use them.
static void SetupVulkanWindow(ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface, int width, int height) {
    wd->Surface = surface;

    // Check for WSI support
    VkBool32 res;
    vkGetPhysicalDeviceSurfaceSupportKHR(g_PhysicalDevice, g_QueueFamily, wd->Surface, &res);
    if (res != VK_TRUE) {
        fprintf(stderr, "Error no WSI support on physical device 0\n");
        exit(-1);
    }

    // Select Surface Format
    const VkFormat requestSurfaceImageFormat[] = { VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB };
    const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
        g_PhysicalDevice,
        wd->Surface,
        requestSurfaceImageFormat,
        (size_t)IM_ARRAYSIZE(requestSurfaceImageFormat),
        requestSurfaceColorSpace
    );

    // Select Present Mode
#ifdef APP_USE_UNLIMITED_FRAME_RATE
    VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR};
#else
    VkPresentModeKHR present_modes[] = {VK_PRESENT_MODE_FIFO_KHR};
#endif
    wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
        g_PhysicalDevice,
        wd->Surface,
        &present_modes[0],
        IM_ARRAYSIZE(present_modes)
    );
    // printf("[vulkan] Selected PresentMode = %d\n", wd->PresentMode);

    // Create SwapChain, RenderPass, Framebuffer, etc.
    IM_ASSERT(g_MinImageCount >= 2);
    ImGui_ImplVulkanH_CreateOrResizeWindow(
        g_Instance,
        g_PhysicalDevice,
        g_Device,
        wd,
        g_QueueFamily,
        g_Allocator,
        width,
        height,
        g_MinImageCount
    );
}

static void CleanupVulkan() {
    vkDestroyDescriptorPool(g_Device, g_DescriptorPool, g_Allocator);

#ifdef APP_USE_VULKAN_DEBUG_REPORT
    // Remove the debug report callback
    auto f_vkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT
    )vkGetInstanceProcAddr(g_Instance, "vkDestroyDebugReportCallbackEXT");
    f_vkDestroyDebugReportCallbackEXT(g_Instance, g_DebugReport, g_Allocator);
#endif // APP_USE_VULKAN_DEBUG_REPORT

    vkDestroyDevice(g_Device, g_Allocator);
    vkDestroyInstance(g_Instance, g_Allocator);
}

static void CleanupVulkanWindow() {
    ImGui_ImplVulkanH_DestroyWindow(g_Instance, g_Device, &g_MainWindowData, g_Allocator);
}

static void FrameRender(ImGui_ImplVulkanH_Window* wd, ImDrawData* draw_data) {
    VkSemaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    VkResult
        err = vkAcquireNextImageKHR(g_Device, wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &wd->FrameIndex);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
        g_SwapChainRebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR)
        return;
    if (err != VK_SUBOPTIMAL_KHR)
        check_vk_result(err);

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];
    {
        err = vkWaitForFences(g_Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX); // wait indefinitely instead of periodically checking
        check_vk_result(err);

        err = vkResetFences(g_Device, 1, &fd->Fence);
        check_vk_result(err);
    }
    {
        err = vkResetCommandPool(g_Device, fd->CommandPool, 0);
        check_vk_result(err);

       
        err = vkBeginCommandBuffer(fd->CommandBuffer, &begin_info);
        check_vk_result(err);
    }
    {
        // Transition image layout
        VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = fd->Backbuffer,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };

        vkCmdPipelineBarrier(
            fd->CommandBuffer,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier
        );

        vkEndCommandBuffer(fd->CommandBuffer);
    }
    {
        VkSubmitInfo submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &fd->CommandBuffer,
        };

        vkQueueSubmit(g_Queue, 1, &submit_info, fd->Fence);

        vkWaitForFences(g_Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
    }
    {
        vr::VRVulkanTextureData_t vulkanTexure{};
        vulkanTexure.m_nImage = (uintptr_t)fd->Backbuffer;
        vulkanTexure.m_pDevice = g_Device;
        vulkanTexure.m_pPhysicalDevice = g_PhysicalDevice;
        vulkanTexure.m_pInstance = g_Instance;
        vulkanTexure.m_pQueue = g_Queue;
        vulkanTexure.m_nQueueFamilyIndex = g_QueueFamily;
        vulkanTexure.m_nWidth = g_MainWindowData.Width;
        vulkanTexure.m_nHeight = g_MainWindowData.Height;
        vulkanTexure.m_nFormat = g_MainWindowData.SurfaceFormat.format;
        vulkanTexure.m_nSampleCount = VK_SAMPLE_COUNT_1_BIT; // no multi sampling

        vr::Texture_t vrTexture{};
        vrTexture.handle = (void*)&vulkanTexure;
        vrTexture.eColorSpace = vr::ColorSpace_Auto;
        vrTexture.eType = vr::TextureType_Vulkan;

        vr::VROverlay()->SetOverlayTexture(g_Overlayhandle, &vrTexture);

        vr::HmdVector2_t overlayMouseScale = {(float)g_MainWindowData.Width, (float)g_MainWindowData.Height};
        vr::VROverlay()->SetOverlayMouseScale(g_Overlayhandle, &overlayMouseScale);
    }
    {
        vkResetCommandPool(g_Device, fd->CommandPool, 0);
        vkBeginCommandBuffer(fd->CommandBuffer, &begin_info);

        err = vkResetFences(g_Device, 1, &fd->Fence);
        check_vk_result(err);
    }
    {
        VkRenderPassBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass = wd->RenderPass;
        info.framebuffer = fd->Framebuffer;
        info.renderArea.extent.width = wd->Width;
        info.renderArea.extent.height = wd->Height;
        info.clearValueCount = 1;
        info.pClearValues = &wd->ClearValue;
        vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
    }

    // Record dear imgui primitives into command buffer
    ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

    // Submit command buffer
    vkCmdEndRenderPass(fd->CommandBuffer);
    {
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &image_acquired_semaphore;
        info.pWaitDstStageMask = &wait_stage;
        info.commandBufferCount = 1;
        info.pCommandBuffers = &fd->CommandBuffer;
        info.signalSemaphoreCount = 1;
        info.pSignalSemaphores = &render_complete_semaphore;

        err = vkEndCommandBuffer(fd->CommandBuffer);
        check_vk_result(err);
        err = vkQueueSubmit(g_Queue, 1, &info, fd->Fence);
        check_vk_result(err);
    }
}

static void FramePresent(ImGui_ImplVulkanH_Window* wd) {
    if (g_SwapChainRebuild)
        return;
    VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
    VkPresentInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &render_complete_semaphore;
    info.swapchainCount = 1;
    info.pSwapchains = &wd->Swapchain;
    info.pImageIndices = &wd->FrameIndex;

    VkResult err = vkQueuePresentKHR(g_Queue, &info);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
        g_SwapChainRebuild = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR)
        return;
    if (err != VK_SUBOPTIMAL_KHR)
        check_vk_result(err);
    wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount; // Now we can use the next set of semaphores
}

// Main code
int main(int, char**) {

    vr::EVRInitError error;

    // Initialize the overlay as "VRApplication_Background" instead of "VRApplication_Overlay"
    // This makes sure that the overlay *cannot* run while SteamVR is not running.
    VR_Init(&error, vr::VRApplication_Background);

    // the overlay should not run when SteamVR us not running
    if (error == vr::VRInitError_Init_NoServerForBackgroundApp) 
    {
        // the user doesn't need to know
        return EXIT_FAILURE;
    }

    printf("VR_Init: %d\n", error);

    // Get user HMD "Prop_DisplayFrequency_Float"
    g_RefreshRate = vr::VRSystem()->GetFloatTrackedDeviceProperty(
        vr::k_unTrackedDeviceIndex_Hmd,
        vr::Prop_DisplayFrequency_Float
    );

    // Setup SDL
    // [If using SDL_MAIN_USE_CALLBACKS: all code below until the main loop starts would likely be your SDL_AppInit() function]
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        printf("Error: SDL_Init(): %s\n", SDL_GetError());
        return -1;
    }

    // Create window with Vulkan graphics context
    SDL_WindowFlags window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN | SDL_WINDOW_UTILITY; // SDL_WINDOW_UTILITY to hide application from Task Manager & Task Bar
    SDL_Window* window = SDL_CreateWindow(
        "LeapEx",
        (int)(1280),
        (int)(720),
        window_flags
    );
    if (window == nullptr) {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return -1;
    }

    SetupVulkan();

    // Create Window Surface
    VkSurfaceKHR surface;
    VkResult err;
    if (SDL_Vulkan_CreateSurface(window, g_Instance, g_Allocator, &surface) == 0) {
        printf("Failed to create Vulkan surface.\n");
        return 1;
    }

    std::string manifestPath{};
    manifestPath += SDL_GetCurrentDirectory();
    manifestPath += "manifest.vrmanifest";

    if (!vr::VRApplications()->IsApplicationInstalled("nyabsi.LeapEx")) 
    {
        auto manifestError = vr::VRApplications()->AddApplicationManifest(manifestPath.data());

        switch (manifestError)
        {
        case vr::VRApplicationError_None: printf("Installed OpenVR manifest from %s\n", manifestPath.data()); break;
        case vr::VRApplicationError_InvalidManifest: printf("Could not find OpenVR manifest at %s\n", manifestPath.data()); break;
        default: break;
        }
    } else {
        printf("OpenVR manifest was already registered\n");
    }

    auto overlayError = vr::VROverlay()->CreateDashboardOverlay("nyabsi.LeapEx", "LeapEx", &g_Overlayhandle, &g_OverlayThumbnailHandle);

    // Only allow a single instance of the overlay to run at once
    if (overlayError == vr::VROverlayError_KeyInUse)
    {
        return 1;
    }

    if (g_Overlayhandle == vr::k_ulOverlayHandleInvalid) {
        printf("Failed to create overlay\n");
        return 1;
    }

    vr::VROverlay()->SetOverlayInputMethod(g_Overlayhandle, vr::VROverlayInputMethod_Mouse);
    vr::VROverlay()->SetOverlayFlag(g_Overlayhandle, vr::VROverlayFlags_SendVRDiscreteScrollEvents, true);
    vr::VROverlay()->SetOverlayWidthInMeters(g_Overlayhandle, 2.5f);

    // Create Framebuffers
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    ImGui_ImplVulkanH_Window* wd = &g_MainWindowData;
    SetupVulkanWindow(wd, surface, w, h);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    // remove this to prevent SDL window from rendering
    SDL_ShowWindow(window);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
    io.IniFilename = nullptr;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForVulkan(window);
    ImGui_ImplVulkan_InitInfo init_info = {};

    // Let's use Vulkan 1.1 for compatibility reasons
    init_info.ApiVersion = VK_API_VERSION_1_1;
    init_info.Instance = g_Instance;
    init_info.PhysicalDevice = g_PhysicalDevice;
    init_info.Device = g_Device;
    init_info.QueueFamily = g_QueueFamily;
    init_info.Queue = g_Queue;
    init_info.PipelineCache = g_PipelineCache;
    init_info.DescriptorPool = g_DescriptorPool;
    init_info.RenderPass = wd->RenderPass;
    init_info.Subpass = 0;
    init_info.MinImageCount = g_MinImageCount;
    init_info.ImageCount = wd->ImageCount;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator = g_Allocator;
    init_info.CheckVkResultFn = check_vk_result;

    ImGui_ImplVulkan_Init(&init_info);

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use
    // ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use
    // an assertion, or display an error and quit).
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    // style.FontSizeBase = 20.0f;
    // io.Fonts->AddFontDefault();
    // io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
    // ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
    // IM_ASSERT(font != nullptr);

    // Our state
    bool show_demo_window = true;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    uint64_t lastFrameTime = SDL_GetTicksNS();
    bool done = false;
    while (!done) {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your
        // copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite
        // your copy of the keyboard data. Generally you may always pass all inputs to dear imgui, and hide them from your
        // application based on those two flags. [If using SDL_MAIN_USE_CALLBACKS: call ImGui_ImplSDL3_ProcessEvent() from your
        // SDL_AppEvent() function]
        SDL_Event event;
        while (SDL_PollEvent(&event)) {

            ImGui_ImplSDL3_ProcessEvent(&event);

            if (event.type == SDL_EVENT_QUIT)
                done = true;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
                done = true;
        }

        vr::VREvent_t vrEvent;
        while (vr::VROverlay()->PollNextOverlayEvent(g_Overlayhandle, &vrEvent, sizeof(vrEvent))) {
            switch (vrEvent.eventType) {
            case vr::VREvent_MouseMove: {
                // OpenGL uses coordinate space Bottom Left == 0,0 where as Vulkan is Top Left == 0,0
                // So we need to flip the y-axis to get the correct mouse position data
                auto [xPos, yPos] = std::pair{vrEvent.data.mouse.x, ImGui::GetIO().DisplaySize.y - vrEvent.data.mouse.y};
                io.AddMousePosEvent(xPos, yPos);
            } break;
            case vr::VREvent_MouseButtonDown:
                io.AddMouseButtonEvent((vrEvent.data.mouse.button & vr::VRMouseButton_Left) == vr::VRMouseButton_Left ? 0 : 1, true);
                break;
            case vr::VREvent_MouseButtonUp:
                io.AddMouseButtonEvent(
                    (vrEvent.data.mouse.button & vr::VRMouseButton_Left) == vr::VRMouseButton_Left ? 0 : 1,
                    false
                );
                break;
            case vr::VREvent_ScrollDiscrete: {
                // TODO: fix
                // const float x = vrEvent.data.scroll.xdelta * 360.0f * 8.0f;
                // const float y = vrEvent.data.scroll.ydelta * 360.0f * 8.0f;
                // io.AddMouseWheelEvent(x, y);
                break;
            }
            case vr::VREvent_Quit: done = true; return false;
            }
        }

        bool debug_bypass_dashboard_active = true;

        // Only render if dashboard is activated
        if (vr::VROverlay() && (vr::VROverlay()->IsActiveDashboardOverlay(g_Overlayhandle) || debug_bypass_dashboard_active))
        {
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2((float)g_MainWindowData.Width, (float)g_MainWindowData.Height);

            // Resize swap chain?
            // Probably not needed as the window size is static
            int fb_width, fb_height;
            SDL_GetWindowSize(window, &fb_width, &fb_height);
            if (fb_width > 0 && fb_height > 0
                && (g_SwapChainRebuild || g_MainWindowData.Width != fb_width || g_MainWindowData.Height != fb_height)) {
                ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
                ImGui_ImplVulkanH_CreateOrResizeWindow(
                    g_Instance,
                    g_PhysicalDevice,
                    g_Device,
                    &g_MainWindowData,
                    g_QueueFamily,
                    g_Allocator,
                    fb_width,
                    fb_height,
                    g_MinImageCount
                );
                g_MainWindowData.FrameIndex = 0;
                g_SwapChainRebuild = false;
            }

            // Start the Dear ImGui frame
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();

            // == Menu Render Begin

            // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn
            // more about Dear ImGui!).
            if (show_demo_window)
                ImGui::ShowDemoWindow(&show_demo_window);

            // 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
            {
                static float f = 0.0f;
                static int counter = 0;

                ImGui::Begin("Hello, world!"); // Create a window called "Hello, world!" and append into it.

                ImGui::Text("This is some useful text.");          // Display some text (you can use a format strings too)
                ImGui::Checkbox("Demo Window", &show_demo_window); // Edit bools storing our window open/close state
                ImGui::Checkbox("Another Window", &show_another_window);

                ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
                ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

                if (ImGui::Button("Button")) // Buttons return true when clicked (most widgets return true when edited/activated)
                    counter++;
                ImGui::SameLine();
                ImGui::Text("counter = %d", counter);

                ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
                ImGui::End();
            }

            // 3. Show another simple window.
            if (show_another_window) {
                ImGui::Begin(
                    "Another Window",
                    &show_another_window
                ); // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when
                   // clicked)
                ImGui::Text("Hello from another window!");
                if (ImGui::Button("Close Me"))
                    show_another_window = false;
                ImGui::End();
            }

            // == Menu Render End

            // Rendering
            ImGui::Render();
            ImDrawData* draw_data = ImGui::GetDrawData();

            wd->ClearValue.color.float32[0] = clear_color.x * clear_color.w;
            wd->ClearValue.color.float32[1] = clear_color.y * clear_color.w;
            wd->ClearValue.color.float32[2] = clear_color.z * clear_color.w;
            wd->ClearValue.color.float32[3] = clear_color.w;

            FrameRender(wd, draw_data);
            FramePresent(wd);

            

            float targetTime = static_cast<float>(1000000000) / g_RefreshRate;
            const uint64_t frameDuration = (SDL_GetTicksNS() - lastFrameTime);

            if (frameDuration < targetTime) 
            {
                SDL_DelayPrecise(targetTime - frameDuration);
            }

            lastFrameTime = SDL_GetTicksNS();
        }
    }

    vr::VROverlay()->DestroyOverlay(g_Overlayhandle);

    // Cleanup
    // [If using SDL_MAIN_USE_CALLBACKS: all code below would likely be your SDL_AppQuit() function]
    err = vkDeviceWaitIdle(g_Device);
    check_vk_result(err);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    CleanupVulkanWindow();
    CleanupVulkan();

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
