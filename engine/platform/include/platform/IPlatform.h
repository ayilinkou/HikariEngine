#pragma once

#include <cstdint>
#include <string>

// Deliberately not vk::Extent2D: Platform links no Vulkan, so the two call
// sites in App that feed a swapchain convert at the boundary.
struct Extent2D
{
    uint32_t Width = 0u;
    uint32_t Height = 0u;
};

struct WindowDesc
{
    uint32_t Width = 1920u;
    uint32_t Height = 1080u;
    std::string Title = "Vulkan App";
    bool bResizable = true;
    bool bBorderless = true;
};

// The windowing/OS seam. SdlPlatform is the only implementation today;
// HeadlessPlatform (step 40) is the second, and having two is what lets the
// engine run in CI with no display attached.
class IPlatform
{
public:
    virtual ~IPlatform() = default;

    virtual bool IsHeadless() const = 0;

    // Size of the drawable surface in *pixels*, which differs from the window
    // size in screen coordinates on high-DPI displays — so this, not the
    // window size, is what the swapchain must be sized against. Reports
    // {0, 0} while the window is minimised.
    virtual Extent2D GetFramebufferExtent() const = 0;

    // Reveals the window, which is created hidden so that initialisation is
    // not visible as a blank frame.
    virtual void Show() = 0;

    virtual void SetRelativeMouseMode(bool bEnabled) = 0;
    virtual void WarpMouse(float x, float y) = 0;

    // TEMPORARY. Vulkan surface creation (moves into rhi at step 35) and the
    // ImGui SDL3 backend (moves into engine/editor at step 53) both still run
    // inside App and need the concrete SDL_Window*. Those two callers are the
    // only permitted users; this disappears once they move.
    virtual void* GetNativeWindowHandle() const = 0;
};
