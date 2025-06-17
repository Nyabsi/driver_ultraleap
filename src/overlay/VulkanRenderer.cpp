#include "VulkanRenderer.h"

#include "VulkanUtils.h"

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

#include <openvr.h>

VulkanRenderer::VulkanRenderer() 
{
    vulkan_instance_ = VK_NULL_HANDLE;
    vulkan_physical_device_ = VK_NULL_HANDLE;
    vulkan_queue_family_ = -1;
    vulkan_allocator_ = nullptr; // we do not use custom allocator, it's here for sake of convinience.
    vulkan_device_ = VK_NULL_HANDLE;
    vulkan_queue_ = VK_NULL_HANDLE;
    vulkan_descriptor_pool_ = VK_NULL_HANDLE;
    vulkan_pipeline_cache_ = VK_NULL_HANDLE;
    minimum_concurrent_image_count_ = 2;
    should_rebuild_swapchain_ = false;
    vulkan_instance_extensions_ = {};
    vulkan_instance_extensions_.clear();
    vulkan_device_extensions_ = {};
    vulkan_device_extensions_.clear();
}

auto VulkanRenderer::Initialize()  -> void
{
    VkResult vk_result = {};

    auto get_instance_extensions = [](const std::vector<std::string>& extensions) -> std::vector<const char*> {
        std::vector<const char*> result;
        for (auto& extension : extensions)
            result.push_back(extension.data());
        return result;
    };

    vulkan_instance_extensions_ = GetVulkanInstanceExtensionsRequiredByOpenVR();
    auto instance_extensions = get_instance_extensions(vulkan_instance_extensions_);

    VkInstanceCreateInfo instance_create_info =
    {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .enabledExtensionCount = (uint32_t)instance_extensions.size(),
        .ppEnabledExtensionNames = instance_extensions.data(),
    };

    vk_result = vkCreateInstance(&instance_create_info, vulkan_allocator_, &vulkan_instance_);
    VK_VALIDATE_RESULT(vk_result);

    vulkan_physical_device_ = ImGui_ImplVulkanH_SelectPhysicalDevice(vulkan_instance_);
    assert(vulkan_physical_device_ != VK_NULL_HANDLE);

    vulkan_queue_family_ = ImGui_ImplVulkanH_SelectQueueFamilyIndex(vulkan_physical_device_);
    assert(vulkan_queue_family_ != (uint32_t)-1);

    auto get_device_extensions = [&](const std::vector<std::string>& extensions) -> std::vector<const char*> {
        std::vector<const char*> result = {};
        for (auto& extension : vulkan_device_extensions_)
            result.push_back(extension.c_str());
        return result;
    };

    vulkan_device_extensions_ = GetVulkanDeviceExtensionsRequiredByOpenVR(vulkan_physical_device_);
    auto device_extensions = get_device_extensions(vulkan_device_extensions_);

    // make sure VK_KHR_swapchain is in device_extensions
    device_extensions.push_back("VK_KHR_swapchain");

    constexpr float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo device_queue_info =
    {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = vulkan_queue_family_,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority
    };

    VkDeviceCreateInfo device_create_info = 
    {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &device_queue_info,
        .enabledExtensionCount = (uint32_t)device_extensions.size(),
        .ppEnabledExtensionNames = device_extensions.data(),
    };

    vk_result = vkCreateDevice(vulkan_physical_device_, &device_create_info, vulkan_allocator_, &vulkan_device_);
    VK_VALIDATE_RESULT(vk_result);

    vkGetDeviceQueue(vulkan_device_, vulkan_queue_family_, 0, &vulkan_queue_);

    VkDescriptorPoolSize pool_sizes[] = {
        {
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 
            IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE
        },
    };

    VkDescriptorPoolCreateInfo pool_info = 
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 0,
    };

    for (VkDescriptorPoolSize& pool_size : pool_sizes)
        pool_info.maxSets += pool_size.descriptorCount;

    pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    vk_result = vkCreateDescriptorPool(vulkan_device_, &pool_info, vulkan_allocator_, &vulkan_descriptor_pool_);
    VK_VALIDATE_RESULT(vk_result);
}

auto VulkanRenderer::SetupWindow(ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface, int width, int height)  -> void
{
    VkResult vk_result = {};

    wd->Surface = surface;

    VkBool32 result;
    vk_result = vkGetPhysicalDeviceSurfaceSupportKHR(vulkan_physical_device_, vulkan_queue_family_, wd->Surface, &result); // Check for WSI support
    VK_VALIDATE_RESULT(vk_result);

    if (result != VK_TRUE) {
        fprintf(stderr, "Error no WSI support on physical device 0\n");
        exit(-1);
    }

    // request R8G8B8A8 (RGBA, instead of ARGB) format for OpenVR
    // All compatible formats can be found at https://github.com/ValveSoftware/openvr/wiki/Vulkan#image-formats
    const VkFormat surface_image_format[] = {
        VK_FORMAT_R8G8B8A8_SRGB
    };

    // make sure colour space is non linear otherwise it will not render on AMD GPUs
    const VkColorSpaceKHR surface_color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
        vulkan_physical_device_,
        wd->Surface,
        surface_image_format,
        (size_t)IM_ARRAYSIZE(surface_image_format),
        surface_color_space
    );

    VkPresentModeKHR present_modes[] = {
        VK_PRESENT_MODE_FIFO_KHR
    };

    wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
        vulkan_physical_device_,
        wd->Surface,
        &present_modes[0],
        IM_ARRAYSIZE(present_modes)
    );

    assert(minimum_concurrent_image_count_ >= 2);
    this->BuildSwapchain(wd, width, height);
}

auto VulkanRenderer::BuildSwapchain(ImGui_ImplVulkanH_Window* wd, int width, int height)  -> void
{
    if (should_rebuild_swapchain_)
        ImGui_ImplVulkan_SetMinImageCount(minimum_concurrent_image_count_);

    ImGui_ImplVulkanH_CreateOrResizeWindow(
        vulkan_instance_,
        vulkan_physical_device_,
        vulkan_device_,
        wd,
        vulkan_queue_family_,
        vulkan_allocator_,
        (uint32_t)width,
        (uint32_t)height,
        minimum_concurrent_image_count_
    );

    wd->FrameIndex = 0;
    should_rebuild_swapchain_ = false;
}

auto VulkanRenderer::Render(ImGui_ImplVulkanH_Window* wd, ImDrawData* draw_data, bool is_minimized, VrOverlay*& overlay) -> void
{
    VkResult vk_result = {};

    VkSemaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;

    vk_result = vkAcquireNextImageKHR(vulkan_device_, wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &wd->FrameIndex);

    if (vk_result == VK_ERROR_OUT_OF_DATE_KHR || vk_result == VK_SUBOPTIMAL_KHR)
        should_rebuild_swapchain_ = true;

    if (vk_result == VK_ERROR_OUT_OF_DATE_KHR)
        return;

    if (vk_result != VK_SUBOPTIMAL_KHR)
        VK_VALIDATE_RESULT(vk_result);

    VkCommandBufferBeginInfo buffer_begin_info = 
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];

    const bool overlay_active = overlay->IsDashboardActive();
    if (overlay_active)
    {
        vk_result = vkWaitForFences(vulkan_device_, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
        VK_VALIDATE_RESULT(vk_result);

        vk_result = vkResetFences(vulkan_device_, 1, &fd->Fence);
        VK_VALIDATE_RESULT(vk_result);

        vk_result = vkResetCommandPool(vulkan_device_, fd->CommandPool, 0);
        VK_VALIDATE_RESULT(vk_result);

        vk_result = vkBeginCommandBuffer(fd->CommandBuffer, &buffer_begin_info);
        VK_VALIDATE_RESULT(vk_result);

        VkSubmitInfo submit_info_barrier = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &fd->CommandBuffer,
        };

        // OpenVR expects the Image layout to be "VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL"
        // https://github.com/ValveSoftware/openvr/wiki/Vulkan#image-layout
        VkImageMemoryBarrier image_barrier_optimal =
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_IMAGE_ASPECT_NONE,
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
            &image_barrier_optimal
        );

        vk_result = vkEndCommandBuffer(fd->CommandBuffer);
        VK_VALIDATE_RESULT(vk_result);

        vk_result = vkQueueSubmit(vulkan_queue_, 1, &submit_info_barrier, fd->Fence);
        VK_VALIDATE_RESULT(vk_result);

        vk_result = vkWaitForFences(vulkan_device_, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
        VK_VALIDATE_RESULT(vk_result);

        vr::VRVulkanTextureData_t vulkanTexure = 
        {
            .m_nImage = (uintptr_t)fd->Backbuffer, 
            .m_pDevice = vulkan_device_,
            .m_pPhysicalDevice = vulkan_physical_device_,
            .m_pInstance = vulkan_instance_,
            .m_pQueue = vulkan_queue_,
            .m_nQueueFamilyIndex = (uint32_t)vulkan_queue_family_,
            .m_nWidth = (uint32_t)wd->Width,
            .m_nHeight = (uint32_t)wd->Height,
            .m_nFormat = (uint32_t)wd->SurfaceFormat.format,
            .m_nSampleCount = VK_SAMPLE_COUNT_1_BIT,
        };

        vr::Texture_t vrTexture =
        { 
            .handle = (void*)&vulkanTexure,
            .eType = vr::TextureType_Vulkan,
            .eColorSpace = vr::ColorSpace_Auto, // vr::ColorSpace_Linear does not work on AMD
        };

        try {
            overlay->SetTexture(vrTexture);
            overlay->SetMouseScale(static_cast<float>(wd->Width), static_cast<float>(wd->Height));
        } catch (std::exception ex) {
            printf("Failed to set overlay texture\n%s\n\n", ex.what());
            return;
        }

        vk_result = vkWaitForFences(vulkan_device_, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
        VK_VALIDATE_RESULT(vk_result);

        vk_result = vkResetFences(vulkan_device_, 1, &fd->Fence);
        VK_VALIDATE_RESULT(vk_result);

        vk_result = vkResetCommandPool(vulkan_device_, fd->CommandPool, 0);
        VK_VALIDATE_RESULT(vk_result);

        vk_result = vkBeginCommandBuffer(fd->CommandBuffer, &buffer_begin_info);
        VK_VALIDATE_RESULT(vk_result);

        // Restore VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL -> VK_IMAGE_LAYOUT_PRESENT_SRC_KHR after Texure is passed to SteamVR
        // https://github.com/ValveSoftware/openvr/wiki/Vulkan#image-layout
        VkImageMemoryBarrier image_barrier_khr =
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_IMAGE_ASPECT_NONE,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
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
            &image_barrier_khr
        );

        vk_result = vkEndCommandBuffer(fd->CommandBuffer);
        VK_VALIDATE_RESULT(vk_result);

        vk_result = vkQueueSubmit(vulkan_queue_, 1, &submit_info_barrier, fd->Fence);
        VK_VALIDATE_RESULT(vk_result);
    }

    if (!is_minimized)
    {
        vk_result = vkWaitForFences(vulkan_device_, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
        VK_VALIDATE_RESULT(vk_result);

        vk_result = vkResetFences(vulkan_device_, 1, &fd->Fence);
        VK_VALIDATE_RESULT(vk_result);

        vk_result = vkResetCommandPool(vulkan_device_, fd->CommandPool, 0);
        VK_VALIDATE_RESULT(vk_result);

        vk_result = vkBeginCommandBuffer(fd->CommandBuffer, &buffer_begin_info);
        VK_VALIDATE_RESULT(vk_result);

        VkRenderPassBeginInfo render_pass_begin_info = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = wd->RenderPass,
            .framebuffer = fd->Framebuffer,
            .renderArea =
                {
                    .extent =
                        {
                            .width = (uint32_t)wd->Width,
                            .height = (uint32_t)wd->Height,
                        },
                },
            .clearValueCount = 1,
            .pClearValues = &wd->ClearValue,
        };

        vkCmdBeginRenderPass(fd->CommandBuffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

        ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

        vkCmdEndRenderPass(fd->CommandBuffer);

        vk_result = vkEndCommandBuffer(fd->CommandBuffer);
        VK_VALIDATE_RESULT(vk_result);

        VkPipelineStageFlags wait_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &image_acquired_semaphore,
            .pWaitDstStageMask = &wait_stage_mask,
            .commandBufferCount = 1,
            .pCommandBuffers = &fd->CommandBuffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &render_complete_semaphore,
        };

        vk_result = vkQueueSubmit(vulkan_queue_, 1, &submit_info, fd->Fence);
        VK_VALIDATE_RESULT(vk_result);
    }
}

auto VulkanRenderer::Present(ImGui_ImplVulkanH_Window* wd, bool is_minimized)  -> void
{
    if (should_rebuild_swapchain_ || is_minimized)
        return;

    VkResult vk_result = {};

    VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;

    VkPresentInfoKHR info = 
    { 
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &render_complete_semaphore,
        .swapchainCount = 1,
        .pSwapchains = &wd->Swapchain,
        .pImageIndices = &wd->FrameIndex,
    };

    vk_result = vkQueuePresentKHR(vulkan_queue_, &info);

    if (vk_result == VK_ERROR_OUT_OF_DATE_KHR || vk_result == VK_SUBOPTIMAL_KHR)
        should_rebuild_swapchain_ = true;

    if (vk_result == VK_ERROR_OUT_OF_DATE_KHR)
        return;

    if (vk_result != VK_SUBOPTIMAL_KHR)
        VK_VALIDATE_RESULT(vk_result);

    wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount;
}

auto VulkanRenderer::Destroy() -> void
{
    vkDestroyDescriptorPool(vulkan_device_, vulkan_descriptor_pool_, vulkan_allocator_);
    vkDestroyDevice(vulkan_device_, vulkan_allocator_);
    vkDestroyInstance(vulkan_instance_, vulkan_allocator_);
}