#include <stdio.h>
#include <stdlib.h>

#include <sstream>
#include <fstream>
#include <vector>

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <openvr.h>

#include "VulkanRenderer.h"
#include "VulkanUtils.h"

#include "VrOverlay.h"
#include "VrUtils.h"

static VulkanRenderer* g_vulkanRenderer = new VulkanRenderer();
static VrOverlay* g_overlay = new VrOverlay();
static ImGui_ImplVulkanH_Window g_MainWindowData = {};

static uint64_t g_last_frame_time = SDL_GetTicksNS();
static float g_hmd_refresh_rate = 60.0f;
static bool g_ticking = true;

#define APP_KEY "nyabsi.LeapEx"

int main(
    [[maybe_unused]] int argc, 
    [[maybe_unused]] char** argv
) {
    // Initialize the overlay as "VRApplication_Background" instead of "VRApplication_Overlay"
    // This makes sure that the overlay *cannot* run while SteamVR is not running.
    try {
        OpenVRInit(vr::VRApplication_Background);
    } catch (std::exception ex) {
        printf("Failed to initialize OpenVR\n%s\n\n", ex.what());
        return EXIT_FAILURE;
    }

    // Get the current HMD refresh rate from device props
    try {
        auto hmd_properties = VrTrackedDeviceProperties::FromDeviceIndex(vr::k_unTrackedDeviceIndex_Hmd);
        g_hmd_refresh_rate = hmd_properties.GetFloat(vr::Prop_DisplayFrequency_Float);
        printf("g_hmdRefreshRate = %02f\n", g_hmd_refresh_rate);
    } catch (std::exception ex) {
        printf("Unable to determine Prop_DisplayFrequency_Float\n%s\n\n", ex.what());
        return EXIT_FAILURE;
    }

    // Install the Manifest from the current directory if it is not found
    try {
        if (!OpenVRManifestInstalled(APP_KEY)) {
            OpenVRManifestInstall();
        }
    } catch (std::exception ex) {
        printf("Failed to install OpenVR manifest\n%s\n\n", ex.what());
        return EXIT_FAILURE;
    }
    
    try {
        g_overlay->CreateDashboardOverlay("nyabsi.LeapEx", "LeapEx");
        // Set the overlay properties
        g_overlay->SetInputMethod(vr::VROverlayInputMethod_Mouse);
        g_overlay->EnableFlag(vr::VROverlayFlags_SendVRDiscreteScrollEvents);
        g_overlay->SetWidth(2.5f);
    } catch (std::exception ex) {
        printf("Failed to create overlay\n%s\n\n", ex.what());
        return EXIT_FAILURE;
    }

    // == SDL Init Begin

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        printf("SDL_Init(): %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    SDL_Window* window = SDL_CreateWindow("LeapEx", 1280, 720, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (window == nullptr) {
        printf("SDL_CreateWindow(): %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    g_vulkanRenderer->Initialize();

    VkSurfaceKHR surface = {};
    if (SDL_Vulkan_CreateSurface(window, g_vulkanRenderer->Instance(), g_vulkanRenderer->Allocator(), &surface) == 0) {
        printf("Failed to create Vulkan surface.\n");
        return 1;
    }

    int initial_width = {};
    int initial_height = {};
    SDL_GetWindowSize(window, &initial_width, &initial_height);

    ImGui_ImplVulkanH_Window* wd = &g_MainWindowData;
    g_vulkanRenderer->SetupWindow(wd, surface, initial_width, initial_height);

    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window); // don't use SDL_ShowWindow to hide the window

    // == SDL Init End

    // == ImGui Init Begin

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_IsSRGB;  // NOTE: ImGuiConfigFlags_IsSRGB is not used by ImGui, used to communicate state.
    
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(static_cast<float>(g_MainWindowData.Width), static_cast<float>(g_MainWindowData.Height));

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

    ImGui_ImplVulkan_InitInfo init_info =
    {
        .ApiVersion = VK_API_VERSION_1_1,
        .Instance = g_vulkanRenderer->Instance(),
        .PhysicalDevice = g_vulkanRenderer->PhysicalDevice(),
        .Device = g_vulkanRenderer->Device(),
        .QueueFamily = g_vulkanRenderer->QueueFamily(),
        .Queue = g_vulkanRenderer->Queue(),
        .DescriptorPool = g_vulkanRenderer->DescriptorPool(),
        .RenderPass = wd->RenderPass,
        .MinImageCount = g_vulkanRenderer->MinimumConcurrentImageCount(),
        .ImageCount = wd->ImageCount,
        .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
        .PipelineCache = g_vulkanRenderer->PipelineCache(),
        .Subpass = 0,
        .Allocator = g_vulkanRenderer->Allocator(),
        .CheckVkResultFn = nullptr,
    };

    ImGui_ImplSDL3_InitForVulkan(window);
    ImGui_ImplVulkan_Init(&init_info);

    // == ImGui Init End

    SDL_Event event = {};
    vr::VREvent_t vr_event = {};

    while (g_ticking) {

        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);

            if (event.type == SDL_EVENT_QUIT)
                g_ticking = false;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
                g_ticking = false;
        }

        
        while (vr::VROverlay()->PollNextOverlayEvent(g_overlay->Handle(), &vr_event, sizeof(vr_event))) 
        {
            switch (vr_event.eventType) {
            case vr::VREvent_MouseMove: {
                // OpenGL uses coordinate space Bottom Left == 0,0 where as Vulkan is Top Left == 0,0
                // So we need to flip the y-axis to get the correct mouse position data
                auto [xPos, yPos] = std::pair{vr_event.data.mouse.x, ImGui::GetIO().DisplaySize.y - vr_event.data.mouse.y};
                io.AddMousePosEvent(xPos, yPos);
            } break;
            case vr::VREvent_MouseButtonDown:
                io.AddMouseButtonEvent((vr_event.data.mouse.button & vr::VRMouseButton_Left) == vr::VRMouseButton_Left ? 0 : 1, true);
                break;
            case vr::VREvent_MouseButtonUp:
                io.AddMouseButtonEvent(
                    (vr_event.data.mouse.button & vr::VRMouseButton_Left) == vr::VRMouseButton_Left ? 0 : 1,
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
            case vr::VREvent_Quit: 
                g_ticking = false; 
                return false;
            }
        }

        // Resize swap chain?
        int fb_width = {};
        int fb_height = {};
        SDL_GetWindowSize(window, &fb_width, &fb_height);

        if ((fb_width > 0 && fb_height > 0) && 
            (
                g_vulkanRenderer->ShouldRebuildSwapChain() || 
                g_MainWindowData.Width != fb_width || 
                g_MainWindowData.Height != fb_height
            )
        ) {
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(static_cast<float>(g_MainWindowData.Width), static_cast<float>(g_MainWindowData.Height));

            g_vulkanRenderer->RebuildSwapChain(g_MainWindowData, fb_width, fb_height);
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // == Menu Render Begin

        {
            static bool show_demo = true;
            ImGui::ShowDemoWindow(&show_demo);
        }

        {
            ImGui::Begin("Hello, world!");
            ImGui::Text("This is some useful text.");
            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            ImGui::End();
        }

        // == Menu Render End

        ImGui::EndFrame();
        ImGui::Render();

        ImDrawData* draw_data = ImGui::GetDrawData();

        const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
        const bool dashboard_active = g_overlay->IsDashboardActive();

        if (dashboard_active || !is_minimized) {
            const ImVec4 background_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

            wd->ClearValue.color.float32[0] = background_color.x * background_color.w;
            wd->ClearValue.color.float32[1] = background_color.y * background_color.w;
            wd->ClearValue.color.float32[2] = background_color.z * background_color.w;
            wd->ClearValue.color.float32[3] = background_color.w;

            g_vulkanRenderer->Render(wd, draw_data, is_minimized, g_overlay);
            g_vulkanRenderer->Present(wd, is_minimized);
        }

        // == Frametime Limiter Logic Start

        float target_time = static_cast<float>(1000000000) / g_hmd_refresh_rate;
        const uint64_t frame_duration = (SDL_GetTicksNS() - g_last_frame_time);

        if (frame_duration < target_time) {
            SDL_DelayPrecise(target_time - frame_duration);
        }

        g_last_frame_time = SDL_GetTicksNS();

        // == Frametime Limiter Logic End
    }

    g_overlay->Destroy();

    VkResult vk_result = vkDeviceWaitIdle(g_vulkanRenderer->Device());
    VK_VALIDATE_RESULT(vk_result);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();

    ImGui::DestroyContext();

    ImGui_ImplVulkanH_DestroyWindow(g_vulkanRenderer->Instance(), g_vulkanRenderer->Device(), &g_MainWindowData, g_vulkanRenderer->Allocator());

    g_vulkanRenderer->Destroy();

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
