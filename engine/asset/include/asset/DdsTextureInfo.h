#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <asset/TextureSettingsSource.h>
#include <core/Extent2D.h>

namespace Hikari::Asset
{

// DDS's representation metadata is separate from the authored opaque/mask
// filtering policy. Straight alpha does not establish alpha-test usage.
enum class DdsAlphaMode : uint8_t
{
    Unknown,
    Straight,
    Premultiplied,
    Opaque,
    Custom
};

enum class DdsPixelLayout : uint8_t
{
    Compressed,
    RGB8,
    BGR8,
    RGBA8,
    BGRA8,
    RGBX8,
    BGRX8
};

struct DdsSubresource
{
    uint32_t Level = 0;
    uint32_t Face = 0;
    uint64_t Offset = 0;
    uint64_t Size = 0;
    uint64_t RowPitch = 0; // Zero for compressed blocks.

    bool operator==(const DdsSubresource&) const = default;
};

struct DdsTextureInfo
{
    Core::Extent2D Extent;
    TextureCompression Format = TextureCompression::BC7;
    DdsPixelLayout PixelLayout = DdsPixelLayout::Compressed;
    std::optional<TextureColorSpace> ColorSpace;
    DdsAlphaMode AlphaMode = DdsAlphaMode::Unknown;
    uint32_t Levels = 1;
    uint32_t Faces = 1;
    // DDS order: all mips of +X, then -X, +Y, -Y, +Z, -Z.
    // Single 2D textures have face zero. Arrays and volumes are rejected.
    std::vector<DdsSubresource> Subresources;

    bool IsCompressed() const { return PixelLayout == DdsPixelLayout::Compressed; }

    bool operator==(const DdsTextureInfo&) const = default;
};

uint32_t FullTextureMipCount(Core::Extent2D extent);
uint32_t DdsPixelBytes(DdsPixelLayout layout);
uint32_t DdsPixelChannels(DdsPixelLayout layout);
TextureSettingsSource DdsSettingsSource(const DdsTextureInfo& info, bool bSettingsMissing = false);

} // namespace Hikari::Asset
