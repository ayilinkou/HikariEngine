#pragma once

#include <core/Extent2D.h>
#include <platform/IPlatform.h>

namespace Hikari::Platform
{

/**
 * IPlatform with no window system behind it at all — the implementation that
 * lets the engine run in CI, where there is no display to open a window on.
 *
 * It needs no SDL. Nothing consumes SDL's Vulkan loader: the RHI reaches Vulkan
 * through vulkan.hpp's own dispatcher, which is why the gpu tests create real
 * devices with no SDL anywhere. So this is a plain class, and constructing one
 * touches no window-system subsystem, initialised or not.
 *
 * Takes a WindowDesc, the same as SdlPlatform, so that the caller builds one
 * description and hands it to whichever implementation it picked — being able
 * to swap the two behind IPlatform is the entire point of the seam. Title,
 * bResizable and bBorderless are ignored; a run with no window has no use for
 * any of them, and a window mode cannot reach here anyway because --headless
 * with --borderless or --fullscreen is rejected at parse time.
 */
class HeadlessPlatform final : public IPlatform
{
public:
    explicit HeadlessPlatform(const WindowDesc& desc);

    HeadlessPlatform(const HeadlessPlatform&) = delete;
    HeadlessPlatform& operator=(const HeadlessPlatform&) = delete;

    bool IsHeadless() const override { return true; }

    /**
     * Fixed for the life of the run. There is no display to be resized by and
     * no window system to report a change, so unlike SdlPlatform's this never
     * returns {0, 0} and never moves.
     */
    Core::Extent2D GetFramebufferExtent() const override { return m_Extent; }

    /**
     * All no-ops, and silent. Each asks the window system for something, and
     * there is no window system; none of them is reachable in a headless run
     * anyway — the window-mode flags are rejected at parse time, and the two
     * cursor calls are driven by key events that never arrive.
     */
    void Show() override {}
    void SetWindowMode(WindowMode mode) override;
    void SetRelativeMouseMode(bool bEnabled) override;
    void WarpMouse(float x, float y) override;

    /**
     * Null, which is what the RHI reads as "no surface": DeviceDesc's
     * Requirements.bPresent is what actually decides, and App derives it from
     * IsHeadless().
     */
    void* GetNativeWindowHandle() const override { return nullptr; }

private:
    Core::Extent2D m_Extent{};
};
} // namespace Hikari::Platform
