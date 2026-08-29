#include <platform/HeadlessPlatform.h>

#include <core/Log.h>

namespace
{
constexpr LogCategory LogHeadless("Headless");

// The size a headless run renders at when --resolution said nothing. There is
// no display to size against, so it can only be a documented constant; this one
// matches SdlPlatform's kFallbackWindowSize, which is what that platform falls
// back to when it cannot query a display either.
//
// Resolved here rather than by the caller so that "zero means the platform
// decides" keeps one meaning across both implementations, instead of being
// SdlPlatform's rule for one path and main()'s for the other.
constexpr Extent2D kDefaultHeadlessSize{1280u, 720u};
} // namespace

HeadlessPlatform::HeadlessPlatform(const WindowDesc& desc)
    : m_Extent(desc.Width == 0u || desc.Height == 0u ? kDefaultHeadlessSize
                                                     : Extent2D{desc.Width, desc.Height})
{
    LogMsg(LogSeverity::Info, LogHeadless, "Running headless at {}x{}", m_Extent.Width,
           m_Extent.Height);
}

void HeadlessPlatform::SetWindowMode(WindowMode) {}

void HeadlessPlatform::SetRelativeMouseMode(bool) {}

void HeadlessPlatform::WarpMouse(float, float) {}
