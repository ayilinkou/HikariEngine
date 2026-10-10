#include <asset/ResolvedTextureSettings.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <utility>

#include <nlohmann/json.hpp>

#include "SettingsJson.h"

namespace Hikari::Asset
{

namespace
{
using Json = nlohmann::json;

constexpr std::array kUsages{
    std::pair{"color", TextureUsage::Color}, std::pair{"normal", TextureUsage::Normal},
    std::pair{"scalar", TextureUsage::Scalar}, std::pair{"packed", TextureUsage::Packed}};
constexpr std::array kFormats{
    std::pair{"auto", TextureCompression::Auto}, std::pair{"bc1", TextureCompression::BC1},
    std::pair{"bc2", TextureCompression::BC2},   std::pair{"bc3", TextureCompression::BC3},
    std::pair{"bc4", TextureCompression::BC4},   std::pair{"bc4_s", TextureCompression::BC4Signed},
    std::pair{"bc5", TextureCompression::BC5},   std::pair{"bc5_s", TextureCompression::BC5Signed},
    std::pair{"bc7", TextureCompression::BC7}};
constexpr std::array kColorSpaces{std::pair{"auto", TextureColorSpace::Auto},
                                  std::pair{"linear", TextureColorSpace::Linear},
                                  std::pair{"srgb", TextureColorSpace::Srgb}};
constexpr std::array kAlphaModes{std::pair{"opaque", TextureAlphaMode::Opaque},
                                 std::pair{"mask", TextureAlphaMode::Mask}};
constexpr std::array<std::string_view, 8> kFields{"version",    "usage",      "format",
                                                  "colorSpace", "mipmaps",    "flipNormalY",
                                                  "alphaMode",  "alphaCutoff"};

template <class T, size_t N>
std::optional<T> ParseToken(std::string_view text,
                            const std::array<std::pair<const char*, T>, N>& choices)
{
    std::string lower(text);
    for (char& character : lower)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    for (const auto& [name, value] : choices)
        if (lower == name)
            return value;
    return std::nullopt;
}

template <class T, size_t N>
std::string_view Name(T value, const std::array<std::pair<const char*, T>, N>& choices)
{
    for (const auto& [name, choice] : choices)
        if (value == choice)
            return name;
    return {};
}

std::string FieldError(std::string_view path, std::string_view field, std::string_view message)
{
    return std::format("{}: Field '{}': {}", path, field, message);
}

bool ValidateSchema(const TextureSettings& settings, std::string_view path, std::string& error)
{
    const auto fail = [&](std::string_view field, std::string_view message)
    {
        error = FieldError(path, field, message);
        return false;
    };
    if (settings.Version != 1)
        return fail("version",
                    std::format("Unsupported schema version {}; expected 1", settings.Version));
    if (ToString(settings.Usage).empty())
        return fail("usage", "Expected color, normal, scalar or packed");
    if (ToString(settings.Format).empty())
        return fail("format", "Unsupported compression format");
    if (ToString(settings.ColorSpace).empty())
        return fail("colorSpace", "Expected auto, linear or srgb");
    if (settings.AlphaMode && ToString(*settings.AlphaMode).empty())
        return fail("alphaMode", "Expected opaque or mask");
    if (!std::isfinite(settings.AlphaCutoff) || settings.AlphaCutoff < 0 ||
        settings.AlphaCutoff > 1)
        return fail("alphaCutoff", "Expected a finite number in [0, 1]");
    return true;
}

uint32_t StoredColorChannels(TextureCompression format)
{
    switch (format)
    {
        case TextureCompression::BC4:
        case TextureCompression::BC4Signed:
            return 1;
        case TextureCompression::BC5:
        case TextureCompression::BC5Signed:
            return 2;
        default:
            return 3;
    }
}

bool HasAlpha(TextureCompression format)
{
    return format == TextureCompression::BC1 || format == TextureCompression::BC2 ||
           format == TextureCompression::BC3 || format == TextureCompression::BC7;
}

TextureCompression DefaultFormat(TextureUsage usage)
{
    if (usage == TextureUsage::Normal)
        return TextureCompression::BC5;
    if (usage == TextureUsage::Scalar)
        return TextureCompression::BC4;
    return TextureCompression::BC7;
}

TextureColorSpace DefaultColorSpace(TextureUsage usage)
{
    return usage == TextureUsage::Color ? TextureColorSpace::Srgb : TextureColorSpace::Linear;
}

} // namespace

std::optional<TextureUsage> ParseTextureUsage(std::string_view value)
{
    return ParseToken(value, kUsages);
}

std::optional<TextureCompression> ParseTextureCompression(std::string_view value)
{
    return ParseToken(value, kFormats);
}

std::optional<TextureColorSpace> ParseTextureColorSpace(std::string_view value)
{
    return ParseToken(value, kColorSpaces);
}

std::optional<TextureAlphaMode> ParseTextureAlphaMode(std::string_view value)
{
    return ParseToken(value, kAlphaModes);
}

std::string_view ToString(TextureUsage value)
{
    return Name(value, kUsages);
}
std::string_view ToString(TextureCompression value)
{
    return Name(value, kFormats);
}
std::string_view ToDisplayString(TextureCompression value)
{
    switch (value)
    {
        case TextureCompression::Auto:
            return "auto";
        case TextureCompression::BC1:
            return "BC1";
        case TextureCompression::BC2:
            return "BC2";
        case TextureCompression::BC3:
            return "BC3";
        case TextureCompression::BC4:
            return "BC4";
        case TextureCompression::BC4Signed:
            return "BC4_S";
        case TextureCompression::BC5:
            return "BC5";
        case TextureCompression::BC5Signed:
            return "BC5_S";
        case TextureCompression::BC7:
            return "BC7";
    }
    return {};
}
std::string_view ToString(TextureColorSpace value)
{
    return Name(value, kColorSpaces);
}
std::string_view ToString(TextureAlphaMode value)
{
    return Name(value, kAlphaModes);
}

std::optional<TextureSettings>
ParseTextureSettings(std::string_view text, std::string_view settingsPath, std::string& error)
{
    const auto parsedDocument = ParseSettingsJson(text, settingsPath, error);
    if (!parsedDocument)
        return std::nullopt;
    const Json& document = *parsedDocument;
    for (const auto& [key, value] : document.items())
    {
        (void)value;
        if (std::find(kFields.begin(), kFields.end(), key) == kFields.end())
        {
            error = FieldError(settingsPath, key, "Unknown field");
            return std::nullopt;
        }
    }
    if (!document.contains("version") || !document.at("version").is_number_integer())
    {
        error = FieldError(settingsPath, "version", "Required integer schema version 1");
        return std::nullopt;
    }
    if (document.at("version") != 1)
    {
        error = FieldError(settingsPath, "version",
                           std::format("Unsupported schema version {}; expected 1",
                                       document.at("version").dump()));
        return std::nullopt;
    }

    TextureSettings settings;
    const auto readEnum =
        [&](std::string_view field, auto parse, auto& target, std::string_view expected)
    {
        const auto found = document.find(std::string(field));
        if (found == document.end())
            return true;
        const auto parsed = found->is_string() ? parse(found->get<std::string>()) : std::nullopt;
        if (!parsed)
        {
            error = FieldError(settingsPath, field, expected);
            return false;
        }
        target = *parsed;
        return true;
    };
    if (!readEnum("usage", ParseTextureUsage, settings.Usage,
                  "Expected color, normal, scalar or packed") ||
        !readEnum("format", ParseTextureCompression, settings.Format,
                  "Expected auto, BC1, BC2, BC3, BC4, BC4_S, BC5, BC5_S or BC7") ||
        !readEnum("colorSpace", ParseTextureColorSpace, settings.ColorSpace,
                  "Expected auto, linear or srgb") ||
        !readEnum("alphaMode", ParseTextureAlphaMode, settings.AlphaMode,
                  "Expected opaque or mask"))
        return std::nullopt;

    const auto readBool = [&](std::string_view field, bool& target)
    {
        const auto found = document.find(std::string(field));
        if (found == document.end())
            return true;
        if (!found->is_boolean())
        {
            error = FieldError(settingsPath, field, "Expected true or false");
            return false;
        }
        target = found->get<bool>();
        return true;
    };
    if (!readBool("mipmaps", settings.bMipmaps) || !readBool("flipNormalY", settings.bFlipNormalY))
        return std::nullopt;
    if (document.contains("alphaCutoff"))
    {
        if (!document.at("alphaCutoff").is_number())
        {
            error = FieldError(settingsPath, "alphaCutoff", "Expected a finite number in [0, 1]");
            return std::nullopt;
        }
        settings.AlphaCutoff = document.at("alphaCutoff").get<double>();
    }
    if (!ValidateSchema(settings, settingsPath, error))
        return std::nullopt;
    return settings;
}

std::optional<std::string> SerializeTextureSettings(const TextureSettings& settings,
                                                    std::string_view settingsPath,
                                                    std::string& error)
{
    error.clear();
    if (!ValidateSchema(settings, settingsPath, error))
        return std::nullopt;
    Json document{
        {"version", settings.Version},         {"usage", ToString(settings.Usage)},
        {"format", ToString(settings.Format)}, {"colorSpace", ToString(settings.ColorSpace)},
        {"mipmaps", settings.bMipmaps},        {"flipNormalY", settings.bFlipNormalY},
        {"alphaCutoff", settings.AlphaCutoff}};
    // Keep an unspecified DDS alpha use unspecified across settings round trips.
    if (settings.AlphaMode)
        document["alphaMode"] = ToString(*settings.AlphaMode);
    return document.dump(2) + '\n';
}

bool TextureSettingsOverrides::IsEmpty() const
{
    return !Usage && !Format && !ColorSpace && !AlphaMode && !AlphaCutoff && !bFlipNormalY &&
           !bNoMips;
}

std::optional<ResolvedTextureSettings>
ResolveTextureSettings(const TextureSettings& saved, const TextureSettingsOverrides& overrides,
                       const TextureSettingsSource& source, std::string_view settingsPath,
                       std::string& error, std::vector<std::string>& warnings, bool bBatch)
{
    error.clear();
    warnings.clear();
    const auto fail = [&](std::string_view field, std::string_view message)
    {
        error = FieldError(settingsPath, field, message);
        return std::optional<ResolvedTextureSettings>{};
    };
    if (bBatch && !overrides.IsEmpty())
        return fail("overrides",
                    "Batch cooking (cook-all) accepts operational options only; change individual "
                    "settings or use single-file cook");

    ResolvedTextureSettings resolved;
    resolved.Authored = saved;
    auto& authored = resolved.Authored;
    if (overrides.Usage)
        authored.Usage = *overrides.Usage;
    if (overrides.Format)
        authored.Format = *overrides.Format;
    if (overrides.ColorSpace)
        authored.ColorSpace = *overrides.ColorSpace;
    if (overrides.AlphaMode)
        authored.AlphaMode = *overrides.AlphaMode;
    if (overrides.AlphaCutoff)
        authored.AlphaCutoff = *overrides.AlphaCutoff;
    authored.bFlipNormalY = authored.bFlipNormalY || overrides.bFlipNormalY;
    authored.bMipmaps = authored.bMipmaps && !overrides.bNoMips;
    if (!ValidateSchema(authored, settingsPath, error))
        return std::nullopt;
    if (source.Channels > 4)
        return fail("source.channels", "Expected an 8-bit source with 1 to 4 channels");
    if (source.NativeFormat && (ToString(*source.NativeFormat).empty() ||
                                *source.NativeFormat == TextureCompression::Auto))
        return fail("source.format", "Native format must be a supported concrete BC format");
    if (source.NativeColorSpace && (ToString(*source.NativeColorSpace).empty() ||
                                    *source.NativeColorSpace == TextureColorSpace::Auto))
        return fail("source.colorSpace", "Native colour space must be linear or srgb");
    if (source.NativeAlphaMode && ToString(*source.NativeAlphaMode).empty())
        return fail("source.alphaMode", "Unsupported native alpha mode");
    if (!source.bDds && (source.NativeFormat || source.NativeColorSpace || source.NativeAlphaMode))
        return fail("source", "Native DDS metadata requires a DDS source");

    resolved.Format = authored.Format;
    if (resolved.Format == TextureCompression::Auto)
        resolved.Format = source.bDds && source.NativeFormat ? *source.NativeFormat
                                                             : DefaultFormat(authored.Usage);
    resolved.ColorSpace = authored.ColorSpace;
    if (resolved.ColorSpace == TextureColorSpace::Auto)
        resolved.ColorSpace = source.bDds && source.NativeColorSpace
                                  ? *source.NativeColorSpace
                                  : DefaultColorSpace(authored.Usage);
    if (!source.bDds && !authored.AlphaMode)
        authored.AlphaMode = TextureAlphaMode::Opaque;
    resolved.AlphaMode = authored.AlphaMode.value_or(
        source.bDds ? source.NativeAlphaMode.value_or(TextureAlphaMode::Opaque)
                    : TextureAlphaMode::Opaque);
    resolved.bAlphaUsageKnown =
        !source.bDds || authored.AlphaMode.has_value() || source.NativeAlphaMode.has_value();
    resolved.bRecompressExistingMips =
        source.bDds && source.NativeFormat &&
        (*source.NativeFormat != resolved.Format || authored.bFlipNormalY);

    const bool bSingleChannel = resolved.Format == TextureCompression::BC4 ||
                                resolved.Format == TextureCompression::BC4Signed;
    const bool bTwoChannels = resolved.Format == TextureCompression::BC5 ||
                              resolved.Format == TextureCompression::BC5Signed;
    if ((bSingleChannel || bTwoChannels) && resolved.ColorSpace == TextureColorSpace::Srgb)
        return fail("colorSpace", "BC4/BC5 have no sRGB variants; select linear and recook");
    if (!source.NativeFormat &&
        (resolved.Format == TextureCompression::BC2 || resolved.Format == TextureCompression::BC3 ||
         resolved.Format == TextureCompression::BC4Signed ||
         resolved.Format == TextureCompression::BC5Signed))
        return fail("format", "New image encoding supports BC1, BC4, BC5 and BC7; BC2/BC3 and "
                              "signed formats require compressed DDS input");
    if (authored.Usage == TextureUsage::Normal)
    {
        if (resolved.ColorSpace != TextureColorSpace::Linear)
            return fail("colorSpace", "Normal usage requires linear colour space");
        if (bSingleChannel)
            return fail("format", "Normal usage requires at least two stored channels");
        if (!source.NativeFormat && source.Channels != 0 && source.Channels < 3)
            return fail(
                "usage",
                "Normal image inputs require RGB/XYZ channels; use a supported normal source");
        if (source.NativeFormat && StoredColorChannels(*source.NativeFormat) < 2)
            return fail("usage", "Normal DDS inputs require at least two native channels");
    }
    if (authored.bFlipNormalY && authored.Usage != TextureUsage::Normal)
        return fail("flipNormalY", "Enabled normal-Y flipping requires normal usage; edit "
                                   "flipNormalY to false to disable it");
    if (resolved.AlphaMode == TextureAlphaMode::Mask)
    {
        if (!HasAlpha(resolved.Format))
            return fail("alphaMode", "Mask usage requires an alpha-capable format");
        if (authored.bMipmaps && source.bNeedsMipGeneration)
            return fail("mipmaps", "Generated masked mips are not supported; use --no-mips "
                                   "(mipmaps: false) or retain supplied DDS mips");
    }

    const auto warn = [&](std::string_view message)
    {
        warnings.push_back(std::format("{}: {} Review this settings file and recook after changes.",
                                       settingsPath, message));
    };
    if (source.bSettingsMissing && !overrides.Usage)
        warn("No saved usage: assuming color.");
    if (source.bDds && !source.NativeColorSpace && authored.ColorSpace == TextureColorSpace::Auto)
        warn(std::format("DDS has no explicit colour-space metadata; assuming {} from usage {}.",
                         ToString(resolved.ColorSpace), ToString(authored.Usage)));
    if (!source.NativeFormat && source.Channels != 0 && source.Channels <= 2 &&
        authored.Usage == TextureUsage::Color && resolved.ColorSpace == TextureColorSpace::Srgb)
        warn("A scalar/grayscale source is configured as sRGB color; check usage and colorSpace.");
    // Two-channel images are grayscale plus alpha; BC5 DDS carries two data
    // channels instead. Loss warnings also must not claim retained blocks change.
    uint32_t sourceColorChannels = std::min(source.Channels, 1u);
    if (source.Channels >= 3)
        sourceColorChannels = 3;
    if (source.NativeFormat)
        sourceColorChannels = StoredColorChannels(*source.NativeFormat);
    const bool bProcessesPixels = !source.NativeFormat || resolved.bRecompressExistingMips ||
                                  (authored.bMipmaps && source.bNeedsMipGeneration);
    if (bProcessesPixels && sourceColorChannels > StoredColorChannels(resolved.Format))
        warn(std::format(
            "Format {} stores {} colour/data channels; remaining source colour/data channels "
            "will be discarded.",
            ToDisplayString(resolved.Format), StoredColorChannels(resolved.Format)));
    const bool bSourceHasAlpha = source.NativeFormat ? HasAlpha(*source.NativeFormat)
                                                     : source.Channels == 2 || source.Channels == 4;
    if (bProcessesPixels && bSourceHasAlpha &&
        (!HasAlpha(resolved.Format) || (resolved.Format == TextureCompression::BC1 &&
                                        resolved.AlphaMode == TextureAlphaMode::Opaque)))
        warn(std::format("Format {} with alphaMode {} discards the source alpha channel.",
                         ToDisplayString(resolved.Format), ToString(resolved.AlphaMode)));
    if ((authored.Usage == TextureUsage::Scalar || authored.Usage == TextureUsage::Packed) &&
        resolved.ColorSpace == TextureColorSpace::Srgb)
        warn("Material data is configured as sRGB; check whether colorSpace should be linear.");
    if (resolved.bRecompressExistingMips)
        warn("Explicit DDS format conversion or pixel changes will recompress existing levels; "
             "byte preservation no longer applies.");
    if (source.bDds && !resolved.bAlphaUsageKnown && bSourceHasAlpha && authored.bMipmaps &&
        source.bNeedsMipGeneration)
        warn("DDS alpha usage is unresolved: ordinary missing-mip filtering does not preserve "
             "alpha-test coverage and can shrink/remove foliage. Set alphaMode to mask and use "
             "--no-mips if needed.");
    return resolved;
}

} // namespace Hikari::Asset
