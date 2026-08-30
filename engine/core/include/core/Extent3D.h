#pragma once

#include <cstdint>

namespace Hikari::Core
{

/**
 * A three-dimensional size, for the resources that have one: a texture's
 * extent, the region a copy covers.
 *
 * Depth defaults to 1 rather than 0 so that a 2D extent written as
 * `{width, height}` is a valid one-slice volume, which is what every 2D texture
 * and every 2D copy region wants. A zero would be rejected by the backend as an
 * empty region, and the default that makes the common case correct is worth
 * more than the symmetry with Extent2D.
 */
struct Extent3D
{
    uint32_t Width = 0u;
    uint32_t Height = 0u;
    uint32_t Depth = 1u;

    constexpr bool operator==(const Extent3D&) const = default;
};
} // namespace Hikari::Core
