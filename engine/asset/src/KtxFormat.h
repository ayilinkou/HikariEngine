#pragma once

#include <cstdint>
#include <optional>

#include <asset/TextureSettings.h>

namespace Hikari::Asset
{

// KTX2's on-disk format identifiers. Container validation needs no graphics
// API types, device or RHI format extension. Tests pin these identifiers to
// the registry constants; the neutral RHI mappings arrive together in TC11.
enum class KtxFormat : uint32_t
{
    BC1RgbUnorm = 131,
    BC1RgbSrgb = 132,
    BC1RgbaUnorm = 133,
    BC1RgbaSrgb = 134,
    BC2Unorm = 135,
    BC2Srgb = 136,
    BC3Unorm = 137,
    BC3Srgb = 138,
    BC4Unorm = 139,
    BC4Snorm = 140,
    BC5Unorm = 141,
    BC5Snorm = 142,
    BC7Unorm = 145,
    BC7Srgb = 146
};

inline std::optional<KtxFormat> NativeKtxFormat(TextureCompression format, TextureColorSpace color)
{
    const bool srgb = color == TextureColorSpace::Srgb;
    if (color == TextureColorSpace::Auto)
        return std::nullopt;
    switch (format)
    {
        case TextureCompression::BC1:
            return srgb ? KtxFormat::BC1RgbaSrgb : KtxFormat::BC1RgbaUnorm;
        case TextureCompression::BC2:
            return srgb ? KtxFormat::BC2Srgb : KtxFormat::BC2Unorm;
        case TextureCompression::BC3:
            return srgb ? KtxFormat::BC3Srgb : KtxFormat::BC3Unorm;
        case TextureCompression::BC7:
            return srgb ? KtxFormat::BC7Srgb : KtxFormat::BC7Unorm;
        case TextureCompression::BC4:
            if (!srgb)
                return KtxFormat::BC4Unorm;
            break;
        case TextureCompression::BC4Signed:
            if (!srgb)
                return KtxFormat::BC4Snorm;
            break;
        case TextureCompression::BC5:
            if (!srgb)
                return KtxFormat::BC5Unorm;
            break;
        case TextureCompression::BC5Signed:
            if (!srgb)
                return KtxFormat::BC5Snorm;
            break;
        case TextureCompression::Auto:
            break;
    }
    return std::nullopt;
}

} // namespace Hikari::Asset
