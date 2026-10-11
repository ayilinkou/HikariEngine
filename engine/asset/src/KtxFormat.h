#pragma once

#include <cstdint>

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

} // namespace Hikari::Asset
