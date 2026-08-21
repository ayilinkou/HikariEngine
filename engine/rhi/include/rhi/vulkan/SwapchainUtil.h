#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <vector>

#include "vulkan/vulkan.hpp"

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

// Tries to get at least 3 images.
inline uint32_t ChooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR& capabilities)
{
    uint32_t minCount = std::max(3u, capabilities.minImageCount);

    // maxImageCount == 0 indicates that there is no maximum
    if ((0 < capabilities.maxImageCount) && (capabilities.maxImageCount < minCount))
        minCount = capabilities.maxImageCount;
    return minCount;
}
