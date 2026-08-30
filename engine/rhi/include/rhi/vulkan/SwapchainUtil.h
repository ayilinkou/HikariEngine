#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <vector>

#include "vulkan/vulkan.hpp"

namespace Hikari::Rhi::Vulkan
{

// Picks the swapchain parameters from what a surface reports it supports.
//
// These are pure functions over the query results — they neither perform the
// queries nor create the swapchain. That is deliberate: it keeps them callable
// from a test without a device, and lets the swapchain abstraction own the
// queries without the choosing logic being buried inside it.

// Chooses an ideal swapchain format if available, if not picks the first
// one.
inline vk::SurfaceFormatKHR ChooseSwapchainFormat(const std::vector<vk::SurfaceFormatKHR>& formats)
{
    if (formats.empty())
        throw std::runtime_error("No surface formats available!");

    const auto formatIt =
        std::ranges::find_if(formats,
                             [](const auto& format)
                             {
                                 return format.format == vk::Format::eB8G8R8A8Unorm &&
                                        format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
                             });

    return formatIt != formats.end() ? *formatIt : formats[0];
}

// Chooses mailbox presentation mode if available. Falls back to FIFO.
inline vk::PresentModeKHR ChoosePresentMode(const std::vector<vk::PresentModeKHR>& modes)
{
    if (modes.empty())
        throw std::runtime_error("No swapchain presentation modes available!");

    const auto modeIt = std::ranges::find_if(modes, [](const auto& mode)
                                             { return mode == vk::PresentModeKHR::eMailbox; });
    return modeIt != modes.end() ? *modeIt : vk::PresentModeKHR::eFifo;
}

// `framebufferExtent` must be the drawable size in pixels (IPlatform::
// GetFramebufferExtent), not the window size in screen coordinates — the two
// differ on high-DPI displays.
inline vk::Extent2D ChooseSwapchainExtent(const vk::SurfaceCapabilitiesKHR& capabilities,
                                          vk::Extent2D framebufferExtent)
{
    // Some window managers allow resolutions which don't match the window. They
    // symbol this with max value of a uint32_t.
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        return capabilities.currentExtent;

    return {std::clamp(framebufferExtent.width, capabilities.minImageExtent.width,
                       capabilities.maxImageExtent.width),
            std::clamp(framebufferExtent.height, capabilities.minImageExtent.height,
                       capabilities.maxImageExtent.height)};
}

// Whether a surface in this state can back a swapchain at all. A window with no
// area reports a zero extent, and VUID-VkSwapchainCreateInfoKHR-imageExtent-01689
// rejects a zero imageExtent, so the only thing to do is wait for the size to
// change. The Win32 and XCB surfaces both allow this: the spec requires
// currentExtent to equal the window size there and says the window size may
// become (0, 0) — a minimized window — "and so a swapchain cannot be created
// until the size changes" (WSI chapter, VkSurfaceCapabilitiesKHR).
//
// The surface has to be asked rather than the window: SDL reports the size a
// window had before it was minimized, because a minimized Win32 window has an
// empty client rect and SDL_GetWindowSizeInPixels falls back to the last one it
// saw. A window-side check for this therefore never fires.
inline bool CanCreateSwapchain(const vk::SurfaceCapabilitiesKHR& capabilities,
                               vk::Extent2D framebufferExtent)
{
    const vk::Extent2D extent = ChooseSwapchainExtent(capabilities, framebufferExtent);
    return extent.width != 0 && extent.height != 0;
}

// Tries to get at least 3 images.
inline uint32_t ChooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR& capabilities)
{
    uint32_t minCount = std::max(3u, capabilities.minImageCount);

    // maxImageCount == 0 indicates that there is no maximum
    if ((0 < capabilities.maxImageCount) && (capabilities.maxImageCount < minCount))
        minCount = capabilities.maxImageCount;
    return minCount;
}
} // namespace Hikari::Rhi::Vulkan
