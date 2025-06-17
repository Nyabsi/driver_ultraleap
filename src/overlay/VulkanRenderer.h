#pragma once

#include <memory>
#include <atomic>
#include <vector>

#include <vulkan/vulkan.h>

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

#include <openvr.h>

class VulkanRenderer {
public:
    explicit VulkanRenderer();
    void Initialize();

    [[nodiscard]] auto Instance() const -> VkInstance { return vulkan_instance_; }
    [[nodiscard]] auto PhysicalDevice() const -> VkPhysicalDevice { return vulkan_physical_device_; }
    [[nodiscard]] auto QueueFamily() const -> uint32_t { return vulkan_queue_family_; }
    [[nodiscard]] auto Allocator() const -> VkAllocationCallbacks* { return vulkan_allocator_; }
    [[nodiscard]] auto Device() const -> VkDevice { return vulkan_device_; }
    [[nodiscard]] auto Queue() const -> VkQueue { return vulkan_queue_; }
    [[nodiscard]] auto DescriptorPool() const -> VkDescriptorPool { return vulkan_descriptor_pool_; }
    [[nodiscard]] auto PipelineCache() const -> VkPipelineCache { return vulkan_pipeline_cache_; }

    [[nodiscard]] auto MinimumConcurrentImageCount() const -> uint32_t { return minimum_concurrent_image_count_; }
    [[nodiscard]] auto ShouldRebuildSwapChain() const -> bool { return should_rebuild_swapchain_; }

    void SetupWindow(ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface, int width, int height);
    void RebuildSwapChain(ImGui_ImplVulkanH_Window& wd, int width, int height);
    void Render(ImGui_ImplVulkanH_Window* wd, ImDrawData* draw_data, bool is_minimized, vr::VROverlayHandle_t overlayHandle);
    void Present(ImGui_ImplVulkanH_Window* wd, bool is_minimized);

    void Destroy();

  private:

    VkInstance vulkan_instance_;
    VkPhysicalDevice vulkan_physical_device_;
    std::atomic<uint32_t> vulkan_queue_family_;
    VkAllocationCallbacks* vulkan_allocator_;
    VkDevice vulkan_device_;
    VkQueue vulkan_queue_;
    VkDescriptorPool vulkan_descriptor_pool_;
    VkPipelineCache vulkan_pipeline_cache_;
    std::atomic<uint32_t> minimum_concurrent_image_count_;
    std::atomic<bool> should_rebuild_swapchain_;
    std::vector<std::string> vulkan_instance_extensions_;
    std::vector<std::string> vulkan_device_extensions_;
};