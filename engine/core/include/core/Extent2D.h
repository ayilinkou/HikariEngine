#pragma once

#include <cstdint>

namespace Hikari::Core
{

/**
 * A size in whatever units its holder deals in — screen coordinates for a
 * window, pixels for a framebuffer, texels for a texture.
 *
 * In Core because Platform and the RHI both need one and neither may depend on
 * the other. It existed twice before this: Platform's had no comparison
 * operator and the RHI's did, so a resize check written against the wrong one
 * silently would not compile — the better outcome of the two, and not one to
 * rely on.
 *
 * Deliberately not vk::Extent2D. Core links no Vulkan, and the RHI's public API
 * may not name a backend type at all, so the conversion happens at the seam
 * inside the backend.
 */
struct Extent2D
{
    uint32_t Width = 0u;
    uint32_t Height = 0u;

    constexpr bool operator==(const Extent2D&) const = default;
};
} // namespace Hikari::Core
