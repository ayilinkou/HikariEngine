#pragma once

#include <cstdint>

#include <core/Extent2D.h>

namespace Hikari::Asset
{

// Source metadata before RGBA expansion, without allocating decoded pixels.
struct ImageInfo
{
    Core::Extent2D Extent;
    uint32_t Channels = 0;
};

} // namespace Hikari::Asset
