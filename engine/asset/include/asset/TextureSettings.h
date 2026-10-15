#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Hikari::Asset
{

enum class TextureUsage : uint8_t
{
    Color,
    Normal,
    Scalar,
    Packed
};

// BC2/BC3 and signed BC4/BC5 are available for DDS preservation/completion.
enum class TextureCompression : uint8_t
{
    Auto,
    BC1,
    BC2,
    BC3,
    BC4,
    BC4Signed,
    BC5,
    BC5Signed,
    BC7
};

enum class TextureColorSpace : uint8_t
{
    Auto,
    Linear,
    Srgb
};

enum class TextureAlphaMode : uint8_t
{
    Opaque,
    Mask
};

/**
 * Source-adjacent schema version 1. Keep auto fields authored as auto so later
 * usage overrides can change defaults without overwriting explicit choices.
 */
struct TextureSettings
{
    uint32_t Version = 1;
    TextureUsage Usage = TextureUsage::Color;
    TextureCompression Format = TextureCompression::Auto;
    TextureColorSpace ColorSpace = TextureColorSpace::Auto;
    bool bMipmaps = true;
    bool bFlipNormalY = false;
    // Absence defaults images to opaque; DDS may supply or lack alpha metadata.
    std::optional<TextureAlphaMode> AlphaMode;
    double AlphaCutoff = 0.5;

    bool operator==(const TextureSettings&) const = default;
};

// Tokens are case-insensitive; serialization uses lower-case values.
std::optional<TextureUsage> ParseTextureUsage(std::string_view value);
std::optional<TextureCompression> ParseTextureCompression(std::string_view value);
std::optional<TextureColorSpace> ParseTextureColorSpace(std::string_view value);
std::optional<TextureAlphaMode> ParseTextureAlphaMode(std::string_view value);

std::string_view ToString(TextureUsage value);
std::string_view ToString(TextureCompression value);
// Display BC names in uppercase while keeping canonical settings tokens unchanged.
std::string_view ToDisplayString(TextureCompression value);
std::string_view ToString(TextureColorSpace value);
std::string_view ToString(TextureAlphaMode value);

/**
 * Require version 1; optional fields use the defaults above. Unknown fields,
 * duplicate keys and invalid types/values fail with a file/field diagnostic.
 * Parsing performs no I/O; semantic validation follows overrides in ResolveTextureSettings.
 */
std::optional<TextureSettings>
ParseTextureSettings(std::string_view text, std::string_view settingsPath, std::string& error);

// Validate schema values before producing JSON; no files are changed here.
std::optional<std::string> SerializeTextureSettings(const TextureSettings& settings,
                                                    std::string_view settingsPath,
                                                    std::string& error);

} // namespace Hikari::Asset
