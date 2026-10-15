#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <asset/TextureSettings.h>
#include <asset/TextureSettingsOverrides.h>
#include <asset/TextureSettingsSource.h>

namespace Hikari::Asset
{

// Effective storage/operations, alongside the authored values to persist.
struct ResolvedTextureSettings
{
    TextureSettings Authored;
    TextureCompression Format = TextureCompression::BC7;
    TextureColorSpace ColorSpace = TextureColorSpace::Srgb;
    TextureAlphaMode AlphaMode = TextureAlphaMode::Opaque;
    // Opaque is the filtering fallback for DDS whose alpha use is unspecified.
    bool bAlphaUsageKnown = true;
    bool bRecompressExistingMips = false;
};

/**
 * Resolve usage, auto fields and supplied overrides, then validate. DDS defaults
 * preserve native format/colour interpretation. Batch requests reject every
 * per-texture override. Failure clears warnings and never mutates saved settings.
 */
std::optional<ResolvedTextureSettings>
ResolveTextureSettings(const TextureSettings& saved, const TextureSettingsOverrides& overrides,
                       const TextureSettingsSource& source, std::string_view settingsPath,
                       std::string& error, std::vector<std::string>& warnings, bool bBatch = false);

} // namespace Hikari::Asset
