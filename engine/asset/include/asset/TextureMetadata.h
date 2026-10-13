#pragma once

#include <cstdint>
#include <optional>

#include <asset/DdsTextureInfo.h>
#include <asset/TextureSettings.h>

namespace Hikari::Asset
{
enum class TextureNormalEncoding : uint8_t
{
    XyzUnorm,
    XyUnormPositiveZ,
    XySnormPositiveZ,
    YxUnormPositiveZ,
    AgUnormPositiveZ
};

/** Missing authored semantics remain unknown, including on direct cooked loading. */
struct TextureMetadata
{
    std::optional<TextureUsage> Usage;
    std::optional<TextureAlphaMode> AlphaMode;
    std::optional<double> AlphaCutoff;
    std::optional<TextureNormalEncoding> NormalEncoding;
    DdsAlphaMode DdsAlpha = DdsAlphaMode::Unknown;
    DdsChannelSwizzle DdsSwizzle = DdsChannelSwizzle::None;
    bool bDdsNormalMap = false;
    bool bPremultipliedAlpha = false;
    // Khronos Data Format primaries, retained independently of linear/sRGB storage.
    uint8_t ColorPrimaries = 0;
};
} // namespace Hikari::Asset
