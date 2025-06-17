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

#include "VulkanRenderer.h"

// #define APP_USE_UNLIMITED_FRAME_RATE
#ifdef _DEBUG
#define APP_USE_VULKAN_DEBUG_REPORT
static VkDebugReportCallbackEXT g_DebugReport = VK_NULL_HANDLE;
#endif

static VulkanRenderer* g_vulkanRenderer = new VulkanRenderer();

static ImGui_ImplVulkanH_Window g_MainWindowData;

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

    // initialize the renderer
    g_vulkanRenderer->Initialize();

    // Create Window Surface
    VkSurfaceKHR surface;
    VkResult err;

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
    g_vulkanRenderer->SetupWindow(wd, surface, w, h);
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
            if (fb_width > 0 && fb_height > 0 && (g_vulkanRenderer->ShouldRebuildSwapChain() || g_MainWindowData.Width != fb_width || g_MainWindowData.Height != fb_height)) 
            {
                g_vulkanRenderer->RebuildSwapChain(g_MainWindowData, fb_width, fb_height);
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

            g_vulkanRenderer->Render(wd, draw_data, g_Overlayhandle);
            g_vulkanRenderer->Present(wd);

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
    err = vkDeviceWaitIdle(g_vulkanRenderer->Device());
    check_vk_result(err);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    ImGui_ImplVulkanH_DestroyWindow(g_vulkanRenderer->Instance(), g_vulkanRenderer->Device(), &g_MainWindowData, g_vulkanRenderer->Allocator());

    g_vulkanRenderer->Destroy();

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
