#pragma once

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

#include <SDL3/SDL.h>

#include "VulkanRenderer.h"

class ImGuiWindow
{
public:
    explicit ImGuiWindow();
    auto InitializeSDLVulkan(SDL_Window*& window, ImGui_ImplVulkanH_Window*& wd, VulkanRenderer*& renderer) -> void;

    auto SendMousePosition(float x, float y) -> void;
    auto SendMouseDown(int button) -> void;
    auto SendMouseUp(int button) -> void;
    auto SendMouseWheel(float y) -> void;

    auto Draw() -> void;

    auto Destroy(ImGui_ImplVulkanH_Window& wd, VulkanRenderer*& renderer) -> void;
};