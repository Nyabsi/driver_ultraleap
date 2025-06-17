#include <stdio.h>
#include <stdlib.h>

#include <sstream>
#include <fstream>
#include <vector>

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_vulkan.h>

#include <openvr.h>

#include "VulkanRenderer.h"
#include "VulkanUtils.h"

#include "ImGuiWindow.h"

#include "VrOverlay.h"
#include "VrUtils.h"

static VulkanRenderer* g_vulkanRenderer = new VulkanRenderer();
static ImGuiWindow* g_imGuiWindow = new ImGuiWindow();
static VrOverlay* g_overlay = new VrOverlay();
static ImGui_ImplVulkanH_Window g_MainWindowData = {};

static uint64_t g_last_frame_time = SDL_GetTicksNS();
static float g_hmd_refresh_rate = 60.0f;
static bool g_ticking = true;

#define APP_KEY     "nyabsi.LeapEx"
#define APP_NAME    "LeapEx Configuration Overlay"

#define WIN_WIDTH   1280
#define WIN_HEIGHT  720

int main(
    [[maybe_unused]] int argc, 
    [[maybe_unused]] char** argv
) {
    // Initialize the overlay as "VRApplication_Background" instead of "VRApplication_Overlay"
    // This makes sure that the overlay *cannot* run while SteamVR is not running.
    try {
        OpenVRInit(vr::VRApplication_Background);
    } catch (...) {
        printf("OpenVRInit\n");
        return EXIT_FAILURE;
    }

    try {
        auto hmd_properties = VrTrackedDeviceProperties::FromDeviceIndex(vr::k_unTrackedDeviceIndex_Hmd);
        g_hmd_refresh_rate = hmd_properties.GetFloat(vr::Prop_DisplayFrequency_Float);
    } catch (...) {
        printf("hmdRefreshRate\n");
        return EXIT_FAILURE;
    }

    try {
        if (!OpenVRManifestInstalled(APP_KEY)) OpenVRManifestInstall();
    } catch (...) {
        printf("AddApplicationManifest\n");
        return EXIT_FAILURE;
    }
    
    try {
        g_overlay->CreateDashboardOverlay(APP_KEY, APP_NAME);
        g_overlay->SetInputMethod(vr::VROverlayInputMethod_Mouse);
        g_overlay->EnableFlag(vr::VROverlayFlags_SendVRDiscreteScrollEvents);
        g_overlay->EnableFlag(vr::VROverlayFlags_EnableClickStabilization);
        g_overlay->SetWidth(2.5f);
    } catch (...) {
        printf("CreateDashboardOverlay\n");
        return EXIT_FAILURE;
    }

    auto sdl_init_flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO;
    if (!SDL_Init(sdl_init_flags)) {
        printf("SDL_Init(): %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    SDL_Window* window = SDL_CreateWindow(APP_NAME, WIN_WIDTH, WIN_HEIGHT, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN | SDL_WINDOW_MOUSE_FOCUS);
    if (window == nullptr) {
        printf("SDL_CreateWindow(): %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    g_vulkanRenderer->Initialize();

    VkSurfaceKHR surface = {};
    if (SDL_Vulkan_CreateSurface(window, g_vulkanRenderer->Instance(), g_vulkanRenderer->Allocator(), &surface) == 0) {
        printf("SDL_Vulkan_CreateSurface(): %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    int initial_width = {};
    int initial_height = {};
    SDL_GetWindowSize(window, &initial_width, &initial_height);

    ImGui_ImplVulkanH_Window* wd = &g_MainWindowData;

    g_vulkanRenderer->SetupWindow(wd, surface, initial_width, initial_height);
    g_imGuiWindow->InitializeSDLVulkan(window, wd, g_vulkanRenderer);

    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_SetWindowFocusable(window, false);
    SDL_CaptureMouse(false);
    SDL_ShowWindow(window);

    SDL_Event event = {};
    vr::VREvent_t vr_event = {};

    while (g_ticking) 
    {
        while (SDL_PollEvent(&event)) 
        {
            ImGui_ImplSDL3_ProcessEvent(&event);

            if (event.type == SDL_EVENT_QUIT)
                g_ticking = false;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
                g_ticking = false;
        }
        
        while (vr::VROverlay()->PollNextOverlayEvent(g_overlay->Handle(), &vr_event, sizeof(vr_event))) 
        {
            switch (vr_event.eventType) 
            {
                case vr::VREvent_MouseMove:
                {
                    // OpenGL uses coordinate space Bottom Left == 0,0 where as Vulkan is Top Left == 0,0
                    // So we need to flip the y-axis to get the correct mouse position data
                    const auto [x, y] = std::pair{vr_event.data.mouse.x, ImGui::GetIO().DisplaySize.y - vr_event.data.mouse.y};
                    g_imGuiWindow->SendMousePosition(x, y);
                    break;
                }
                case vr::VREvent_MouseButtonDown:
                {
                    int mouse_button = ImGuiMouseButton_COUNT;

                    if (vr_event.data.mouse.button & vr::VRMouseButton_Left)
                        mouse_button = ImGuiMouseButton_Left;
                    if (vr_event.data.mouse.button & vr::VRMouseButton_Right)
                        mouse_button = ImGuiMouseButton_Right;
                    if (vr_event.data.mouse.button & vr::VRMouseButton_Middle)
                        mouse_button = ImGuiMouseButton_Middle;

                    if (mouse_button < ImGuiMouseButton_COUNT)
                        g_imGuiWindow->SendMouseDown(mouse_button);
                    break;
                }
                case vr::VREvent_MouseButtonUp:
                {
                    int mouse_button = ImGuiMouseButton_COUNT;

                    if (vr_event.data.mouse.button & vr::VRMouseButton_Left)
                        mouse_button = ImGuiMouseButton_Left;
                    if (vr_event.data.mouse.button & vr::VRMouseButton_Right)
                        mouse_button = ImGuiMouseButton_Right;
                    if (vr_event.data.mouse.button & vr::VRMouseButton_Middle)
                        mouse_button = ImGuiMouseButton_Middle;

                    if (mouse_button < ImGuiMouseButton_COUNT)
                        g_imGuiWindow->SendMouseUp(mouse_button);
                    break;
                }
                case vr::VREvent_ScrollDiscrete: 
                {
                    // Emulate "physical" mouse behaviour by only sending y-axis
                    // 1.0f == Scrolling Up, -1.0f == Scrolling Down
                    const float y = vr_event.data.scroll.ydelta;
                    if (y != 0.0f)
                        g_imGuiWindow->SendMouseWheel(y);
                    break;
                }
                case vr::VREvent_Quit:
                {
                    g_ticking = false;
                    return false;
                }
            }
        }

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

            g_vulkanRenderer->BuildSwapchain(wd, fb_width, fb_height);
        }

        g_imGuiWindow->Draw();

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

        float target_time = static_cast<float>(1000000000) / g_hmd_refresh_rate;
        const uint64_t frame_duration = (SDL_GetTicksNS() - g_last_frame_time);

        if (frame_duration < target_time) {
            SDL_DelayPrecise(target_time - frame_duration);
        }

        g_last_frame_time = SDL_GetTicksNS();
    }

    VkResult vk_result = vkDeviceWaitIdle(g_vulkanRenderer->Device());
    VK_VALIDATE_RESULT(vk_result);

    g_overlay->Destroy();
    g_imGuiWindow->Destroy(g_MainWindowData, g_vulkanRenderer);
    g_vulkanRenderer->Destroy();

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
