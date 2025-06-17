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

static VulkanRenderer* g_vulkanRenderer = new VulkanRenderer();
static ImGui_ImplVulkanH_Window g_MainWindowData;

static vr::VROverlayHandle_t g_Overlayhandle = NULL;
static vr::VROverlayHandle_t g_OverlayThumbnailHandle = NULL;
static float g_RefreshRate = 60.0f; // default

int main(int, char**) {

    vr::EVRInitError error;

    // Initialize the overlay as "VRApplication_Background" instead of "VRApplication_Overlay"
    // This makes sure that the overlay *cannot* run while SteamVR is not running.
    VR_Init(&error, vr::VRApplication_Background);

    // the overlay should not run when SteamVR us not running
    if (error == vr::VRInitError_Init_NoServerForBackgroundApp) {
        // the user doesn't need to know
        return EXIT_FAILURE;
    }

    printf("VR_Init: %d\n", error);

    // Get user HMD "Prop_DisplayFrequency_Float"
    g_RefreshRate = vr::VRSystem()->GetFloatTrackedDeviceProperty(
        vr::k_unTrackedDeviceIndex_Hmd,
        vr::Prop_DisplayFrequency_Float
    );

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        printf("Error: SDL_Init(): %s\n", SDL_GetError());
        return -1;
    }

    // SDL_WINDOW_UTILITY to hide application from Task Manager & Task Bar
    SDL_Window* window = SDL_CreateWindow("LeapEx", 1280, 720, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN | SDL_WINDOW_UTILITY);
    if (window == nullptr) {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return -1;
    }

    g_vulkanRenderer->Initialize();

    VkSurfaceKHR surface;
    if (SDL_Vulkan_CreateSurface(window, g_vulkanRenderer->Instance(), g_vulkanRenderer->Allocator(), &surface) == 0) {
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
    if (overlayError == vr::VROverlayError_KeyInUse) {
        return 1;
    }

    if (g_Overlayhandle == vr::k_ulOverlayHandleInvalid) {
        printf("Failed to create overlay\n");
        return 1;
    }

    vr::VROverlay()->SetOverlayInputMethod(g_Overlayhandle, vr::VROverlayInputMethod_Mouse);
    vr::VROverlay()->SetOverlayFlag(g_Overlayhandle, vr::VROverlayFlags_SendVRDiscreteScrollEvents, true);
    vr::VROverlay()->SetOverlayWidthInMeters(g_Overlayhandle, 2.5f);

    int w, h;
    SDL_GetWindowSize(window, &w, &h);

    ImGui_ImplVulkanH_Window* wd = &g_MainWindowData;
    g_vulkanRenderer->SetupWindow(wd, surface, w, h);

    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    // don't use SDL_ShowWindow to hide the window
    SDL_ShowWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
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

    ImGui_ImplSDL3_InitForVulkan(window);
    ImGui_ImplVulkan_InitInfo init_info = {};

    init_info.ApiVersion = VK_API_VERSION_1_1; // Let's use Vulkan 1.1 for compatibility reasons
    init_info.Instance = g_vulkanRenderer->Instance();
    init_info.PhysicalDevice = g_vulkanRenderer->PhysicalDevice();
    init_info.Device = g_vulkanRenderer->Device();
    init_info.QueueFamily = g_vulkanRenderer->QueueFamily();
    init_info.Queue = g_vulkanRenderer->Queue();
    init_info.PipelineCache = g_vulkanRenderer->PipelineCache();
    init_info.DescriptorPool = g_vulkanRenderer->DescriptorPool();
    init_info.RenderPass = wd->RenderPass;
    init_info.Subpass = 0;
    init_info.MinImageCount = g_vulkanRenderer->MinimumConcurrentImageCount();
    init_info.ImageCount = wd->ImageCount;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator = g_vulkanRenderer->Allocator();
    init_info.CheckVkResultFn = nullptr; // don't check results from ImGui

    ImGui_ImplVulkan_Init(&init_info);

    bool show_demo_window = true;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    uint64_t lastFrameTime = SDL_GetTicksNS();
    bool done = false;
    while (!done) {

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

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)g_MainWindowData.Width, (float)g_MainWindowData.Height);

        // Resize swap chain?
        // Probably not needed as the window size is static

        int fb_width, fb_height;
        SDL_GetWindowSize(window, &fb_width, &fb_height);
        if (fb_width > 0 && fb_height > 0
            && (g_vulkanRenderer->ShouldRebuildSwapChain() || g_MainWindowData.Width != fb_width
                || g_MainWindowData.Height != fb_height)) {
            g_vulkanRenderer->RebuildSwapChain(g_MainWindowData, fb_width, fb_height);
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // == Menu Render Begin

        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        {
            static float f = 0.0f;
            static int counter = 0;

            ImGui::Begin("Hello, world!");

            ImGui::Text("This is some useful text.");
            ImGui::Checkbox("Demo Window", &show_demo_window);

            ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
            ImGui::ColorEdit3("clear color", (float*)&clear_color);

            if (ImGui::Button("Button"))
                counter++;

            ImGui::SameLine();
            ImGui::Text("counter = %d", counter);

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            ImGui::End();
        }

        // == Menu Render End

        ImGui::EndFrame();
        ImGui::Render();

        ImDrawData* draw_data = ImGui::GetDrawData();

        wd->ClearValue.color.float32[0] = clear_color.x * clear_color.w;
        wd->ClearValue.color.float32[1] = clear_color.y * clear_color.w;
        wd->ClearValue.color.float32[2] = clear_color.z * clear_color.w;
        wd->ClearValue.color.float32[3] = clear_color.w;

        g_vulkanRenderer->Render(wd, draw_data, g_Overlayhandle);
        g_vulkanRenderer->Present(wd);

        float targetTime = static_cast<float>(1000000000) / g_RefreshRate;
        const uint64_t frameDuration = (SDL_GetTicksNS() - lastFrameTime);

        if (frameDuration < targetTime) {
            SDL_DelayPrecise(targetTime - frameDuration);
        }

        lastFrameTime = SDL_GetTicksNS();
    }

    vr::VROverlay()->DestroyOverlay(g_Overlayhandle);

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
