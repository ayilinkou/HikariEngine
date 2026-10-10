#pragma once

#include <cstdint>
#include <optional>

#include <asset/TextureSettings.h>

namespace Hikari::Asset
{

// Metadata from decoding/parsing, never inferred usage from pixel contents.
struct TextureSettingsSource
{
    // Original channels before RGBA expansion; zero means not yet available.
    uint32_t Channels = 0;
    bool bDds = false;
    bool bSettingsMissing = false;
    // Whether completing the requested chain would require newly filtered levels.
    bool bNeedsMipGeneration = true;
    std::optional<TextureCompression> NativeFormat;
    std::optional<TextureColorSpace> NativeColorSpace;
    std::optional<TextureAlphaMode> NativeAlphaMode;
};

} // namespace Hikari::Asset
