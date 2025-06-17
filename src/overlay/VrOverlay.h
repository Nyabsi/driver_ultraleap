#pragma once

#include <stdexcept>
#include <format>

#include <openvr.h>

class VrOverlay {
public:
    explicit VrOverlay()
        : handle(vr::k_ulOverlayHandleInvalid),
          thumbnail_handle(vr::k_ulOverlayHandleInvalid) {}

    [[nodiscard]] auto Handle() const -> vr::VROverlayHandle_t { return handle; }

    [[maybe_unused]] auto CreateDashboardOverlay(const char* key, const char* name) -> void {
        vr::EVROverlayError result = vr::VROverlay()->CreateDashboardOverlay(key, name, &handle, &thumbnail_handle);
        if (result > vr::VROverlayError_None)
            throw std::runtime_error(
                std::format("Failed to create dashboard overlay \"{}\" (\"{}\"): {}", name, key, static_cast<int>(result))
            );
    }

    [[maybe_unused]] auto SetInputMethod(vr::VROverlayInputMethod method) -> void {
        vr::EVROverlayError result = vr::VROverlay()->SetOverlayInputMethod(handle, method);
        if (result > vr::VROverlayError_None)
            throw std::runtime_error(
                std::format("Failed to set overlay input method \"{}\": {}", static_cast<int>(method), static_cast<int>(result))
            );
    }

    [[maybe_unused]] auto EnableFlag(vr::VROverlayFlags flag) -> void {
        vr::EVROverlayError result = vr::VROverlay()->SetOverlayFlag(handle, flag, true);
        if (result > vr::VROverlayError_None)
            throw std::runtime_error(
                std::format("Failed to enable overlay flag \"{}\": {}", static_cast<int>(flag), static_cast<int>(result))
            );
    }

    [[maybe_unused]] auto DisableFlag(vr::VROverlayFlags flag) -> void {
        vr::EVROverlayError result = vr::VROverlay()->SetOverlayFlag(handle, flag, false);
        if (result > vr::VROverlayError_None)
            throw std::runtime_error(
                std::format("Failed to disable overlay flag \"{}\": {}", static_cast<int>(flag), static_cast<int>(result))
            );
    }

    [[maybe_unused]] auto SetWidth(float width) -> void {
        vr::EVROverlayError result = vr::VROverlay()->SetOverlayWidthInMeters(handle, width);
        if (result > vr::VROverlayError_None)
            throw std::runtime_error(std::format("Failed to set overlay width \"{}\": {}", width, static_cast<int>(result)));
    }

    [[maybe_unused]] auto Destroy() -> void {
        vr::VROverlay()->DestroyOverlay(handle);
    }

private:
    vr::VROverlayHandle_t handle;
    vr::VROverlayHandle_t thumbnail_handle;
};