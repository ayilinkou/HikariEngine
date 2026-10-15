#pragma once

#include <cstddef>
#include <cstdint>

#include <core/Extent3D.h>

namespace Hikari::Asset
{
/** A tightly packed mip/face range within TextureData::Data. */
struct TextureSubresource
{
    uint32_t MipLevel = 0;
    uint32_t Face = 0;
    Core::Extent3D Extent{};
    size_t Offset = 0;
    size_t Size = 0;
};
} // namespace Hikari::Asset
