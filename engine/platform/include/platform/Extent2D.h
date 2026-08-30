#pragma once

#include <cstdint>

namespace Hikari::Platform
{

// A size in whatever units its holder deals in — screen coordinates for a
// window, pixels for a framebuffer.
//
// Deliberately not vk::Extent2D: Platform links no Vulkan, so the two call
// sites in App that feed a swapchain convert at the boundary.
struct Extent2D
{
    uint32_t Width = 0u;
    uint32_t Height = 0u;
};
} // namespace Hikari::Platform
