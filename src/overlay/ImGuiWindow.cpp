#include "ImGuiWindow.h"

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

ImGuiWindow::ImGuiWindow() 
{

}

auto ImGuiWindow::InitializeSDLVulkan(SDL_Window*& window, ImGui_ImplVulkanH_Window*& wd, VulkanRenderer*& renderer) -> void 
{
    IMGUI_CHECKVERSION();

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_IsSRGB; // NOTE: ImGuiConfigFlags_IsSRGB is not used by ImGui, used to communicate state.

    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(static_cast<float>(wd->Width), static_cast<float>(wd->Height));

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();

    if (io.ConfigFlags & ImGuiConfigFlags_IsSRGB) {
        // hack: ImGui doesn't handle sRGB colour spaces properly so convert from Linear -> sRGB
        // https://github.com/ocornut/imgui/issues/8271#issuecomment-2564954070
        // remove when these are merged:
        //  https://github.com/ocornut/imgui/pull/8110
        //  https://github.com/ocornut/imgui/pull/8111
        for (int i = 0; i < ImGuiCol_COUNT; i++) {
            ImVec4& col = style.Colors[i];
            col.x = col.x <= 0.04045f ? col.x / 12.92f : pow((col.x + 0.055f) / 1.055f, 2.4f);
            col.y = col.y <= 0.04045f ? col.y / 12.92f : pow((col.y + 0.055f) / 1.055f, 2.4f);
            col.z = col.z <= 0.04045f ? col.z / 12.92f : pow((col.z + 0.055f) / 1.055f, 2.4f);
        }
    }

    ImGui_ImplVulkan_InitInfo init_info = {
        .ApiVersion = VK_API_VERSION_1_1,
        .Instance = renderer->Instance(),
        .PhysicalDevice = renderer->PhysicalDevice(),
        .Device = renderer->Device(),
        .QueueFamily = renderer->QueueFamily(),
        .Queue = renderer->Queue(),
        .DescriptorPool = renderer->DescriptorPool(),
        .RenderPass = wd->RenderPass,
        .MinImageCount = renderer->MinimumConcurrentImageCount(),
        .ImageCount = wd->ImageCount,
        .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
        .PipelineCache = renderer->PipelineCache(),
        .Subpass = 0,
        .Allocator = renderer->Allocator(),
        .CheckVkResultFn = nullptr,
    };

    ImGui_ImplSDL3_InitForVulkan(window);
    ImGui_ImplVulkan_Init(&init_info);
}

auto ImGuiWindow::SendMousePosition(float x, float y) -> void 
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);
}

auto ImGuiWindow::SendMouseDown(int button) -> void 
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseButtonEvent(button, true);
}

auto ImGuiWindow::SendMouseUp(int button) -> void 
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseButtonEvent(button, false);
}

auto ImGuiWindow::SendMouseWheel(float y) -> void 
{
    ImGuiIO& io = ImGui::GetIO();
    // Emulate "physical" mouse behaviour by only sending y-axis
    // 1.0f == Scrolling Up, -1.0f == Scrolling Down
    io.AddMouseWheelEvent(0.0f, y);
}

auto ImGuiWindow::Draw() -> void 
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // == Menu Render Begin

    {
        static bool show_demo = true;
        ImGui::ShowDemoWindow(&show_demo);
    }

    {
        ImGuiIO& io = ImGui::GetIO();

        ImGui::Begin("Hello, world!");
        ImGui::Text("This is some useful text.");
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
        ImGui::End();
    }

    // == Menu Render End

    ImGui::EndFrame();
    ImGui::Render();
}

auto ImGuiWindow::Destroy(ImGui_ImplVulkanH_Window& wd, VulkanRenderer*& renderer) -> void 
{
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    ImGui_ImplVulkanH_DestroyWindow(
        renderer->Instance(),
        renderer->Device(),
        &wd,
        renderer->Allocator()
    );
}