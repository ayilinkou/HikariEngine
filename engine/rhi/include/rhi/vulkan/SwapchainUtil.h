#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

#include "vulkan/vulkan.hpp"

namespace Hikari::Rhi::Vulkan
{

/**
 * Picks the swapchain parameters from what a surface reports it supports.
 *
 * These are pure functions over the query results — they neither perform the
 * queries nor create the swapchain. That is deliberate: it keeps them callable
 * from a test without a device, and lets the swapchain abstraction own the
 * queries without the choosing logic being buried inside it.
 */

/**
 * Chooses the swapchain format from an ordered preference, and fails when the
 * surface offers none of them.
 *
 * A curated order rather than a fallback to formats[0], for two reasons.
 *
 * It is a *rendering* hazard. Both candidates here are UNORM with an
 * sRGB-nonlinear colour space, so whichever is chosen the hardware does the
 * same thing on write and the image is identical. Falling back to whatever the
 * surface listed first can land on an _SRGB format, which encodes on write: the
 * same shader output becomes different pixels, and a baseline comparison fails
 * for a reason that has nothing to do with the change under test.
 *
 * And it is a *naming* hazard. The caller converts the result with
 * FromNativeFormat, which throws on anything the curated Rhi::Format list
 * cannot name. Rhi::Format has BGRA8Unorm and RGBA8Unorm but no BGRA8Srgb — and
 * on an X11 surface with RADV the only two formats offered are B8G8R8A8_SRGB
 * and B8G8R8A8_UNORM, so formats[0] is exactly the unnameable one. The old
 * fallback therefore aborted startup one line later, with a message about
 * format conversion rather than about the surface.
 *
 * Extending the list is a deliberate act: a third entry has to be nameable by
 * Rhi::Format *and* leave the image unchanged, or it is not a fallback.
 */
inline vk::SurfaceFormatKHR ChooseSwapchainFormat(const std::vector<vk::SurfaceFormatKHR>& formats)
{
    if (formats.empty())
        throw std::runtime_error("No surface formats available!");

    constexpr std::array kPreferred{vk::Format::eB8G8R8A8Unorm, vk::Format::eR8G8B8A8Unorm};

    for (const vk::Format preferred : kPreferred)
    {
        const auto formatIt =
            std::ranges::find_if(formats,
                                 [preferred](const auto& format)
                                 {
                                     return format.format == preferred &&
                                            format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
                                 });

        if (formatIt != formats.end())
            return *formatIt;
    }

    // Names what was wanted and what the surface has, because the two together
    // are what a reader needs: the failure is a driver or platform whose
    // surface offers something this list has not met yet.
    std::string offered;
    for (const vk::SurfaceFormatKHR& format : formats)
    {
        if (!offered.empty())
            offered += ", ";
        offered +=
            std::format("{} / {}", vk::to_string(format.format), vk::to_string(format.colorSpace));
    }

    throw std::runtime_error(
        std::format("No usable swapchain format: this surface offers none of B8G8R8A8_UNORM or "
                    "R8G8B8A8_UNORM with SRGB_NONLINEAR. Offered: {}.",
                    offered));
}

/** Chooses mailbox presentation mode if available. Falls back to FIFO. */
inline vk::PresentModeKHR ChoosePresentMode(const std::vector<vk::PresentModeKHR>& modes)
{
    if (modes.empty())
        throw std::runtime_error("No swapchain presentation modes available!");

    const auto modeIt = std::ranges::find_if(modes, [](const auto& mode)
                                             { return mode == vk::PresentModeKHR::eMailbox; });
    return modeIt != modes.end() ? *modeIt : vk::PresentModeKHR::eFifo;
}

/**
 * `framebufferExtent` must be the drawable size in pixels (IPlatform::
 * GetFramebufferExtent), not the window size in screen coordinates — the two
 * differ on high-DPI displays.
 */
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

/**
 * Whether a surface in this state can back a swapchain at all. A window with no
 * area reports a zero extent, and VUID-VkSwapchainCreateInfoKHR-imageExtent-01689
 * rejects a zero imageExtent, so the only thing to do is wait for the size to
 * change. The Win32 and XCB surfaces both allow this: the spec requires
 * currentExtent to equal the window size there and says the window size may
 * become (0, 0) — a minimized window — "and so a swapchain cannot be created
 * until the size changes" (WSI chapter, VkSurfaceCapabilitiesKHR).
 *
 * The surface has to be asked rather than the window: SDL reports the size a
 * window had before it was minimized, because a minimized Win32 window has an
 * empty client rect and SDL_GetWindowSizeInPixels falls back to the last one it
 * saw. A window-side check for this therefore never fires.
 */
inline bool CanCreateSwapchain(const vk::SurfaceCapabilitiesKHR& capabilities,
                               vk::Extent2D framebufferExtent)
{
    const vk::Extent2D extent = ChooseSwapchainExtent(capabilities, framebufferExtent);
    return extent.width != 0 && extent.height != 0;
}

/** Tries to get at least 3 images. */
inline uint32_t ChooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR& capabilities)
{
    uint32_t minCount = std::max(3u, capabilities.minImageCount);

    // maxImageCount == 0 indicates that there is no maximum
    if ((0 < capabilities.maxImageCount) && (capabilities.maxImageCount < minCount))
        minCount = capabilities.maxImageCount;
    return minCount;
}
} // namespace Hikari::Rhi::Vulkan
