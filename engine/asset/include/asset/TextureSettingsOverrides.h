#pragma once

#include <optional>

#include <asset/TextureSettings.h>

namespace Hikari::Asset
{

/**
 * Only supplied single-file options change saved values. Positive-only flags
 * cannot disable or toggle an already enabled setting; disabling is a JSON edit.
 */
struct TextureSettingsOverrides
{
    std::optional<TextureUsage> Usage;
    std::optional<TextureCompression> Format;
    std::optional<TextureColorSpace> ColorSpace;
    std::optional<TextureAlphaMode> AlphaMode;
    std::optional<double> AlphaCutoff;
    bool bFlipNormalY = false;
    bool bNoMips = false;

    bool IsEmpty() const;
};

} // namespace Hikari::Asset
