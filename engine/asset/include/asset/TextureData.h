#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <asset/TextureMetadata.h>
#include <asset/TextureSubresource.h>
#include <core/Extent2D.h>
#include <rhi/RhiTypes.h>

namespace Hikari::Asset
{
/** Owned native blocks, independent of libktx objects and GPU resource lifetimes. */
struct TextureData
{
    Rhi::Format Format = Rhi::Format::Undefined;
    Core::Extent2D Extent{};
    uint32_t MipLevels = 1;
    uint32_t Faces = 1; // One 2D image or +X, -X, +Y, -Y, +Z, -Z.
    TextureMetadata Metadata;
    std::vector<std::byte> Data;
    std::vector<TextureSubresource> Subresources; // Mip-major, then face order.
};
} // namespace Hikari::Asset
