#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <asset/ImageReader.h>
#include <asset/ResolvedTextureSettings.h>

using namespace Hikari::Asset;

namespace
{
bool Contains(std::string_view text, std::string_view part)
{
    return text.find(part) != std::string_view::npos;
}

constexpr std::string_view kSettingsPath = "content/textures/example.png.json";

struct SettingsFixture
{
    TextureSettings Saved;
    TextureSettingsOverrides Overrides;
    TextureSettingsSource Source;
    std::string Error;
    std::vector<std::string> Warnings;

    std::optional<ResolvedTextureSettings> Resolve(bool bBatch = false)
    {
        return ResolveTextureSettings(Saved, Overrides, Source, kSettingsPath, Error, Warnings,
                                      bBatch);
    }

    TextureSettings Parse(std::string_view text)
    {
        const auto parsed = ParseTextureSettings(text, kSettingsPath, Error);
        INFO(Error);
        REQUIRE(parsed);
        return *parsed;
    }

    void Reload(const TextureSettings& settings)
    {
        const auto serialized = SerializeTextureSettings(settings, kSettingsPath, Error);
        INFO(Error);
        REQUIRE(serialized);
        Saved = Parse(*serialized);
        Overrides = {};
    }

    bool WarnsAbout(std::string_view text) const
    {
        return std::any_of(Warnings.begin(), Warnings.end(),
                           [&](const std::string& warning) { return Contains(warning, text); });
    }
};
} // namespace

TEST_CASE("Texture settings require a known schema and reject malformed fields",
          "[asset][texture-settings]")
{
    struct Invalid
    {
        std::string_view Json;
        std::string_view Field;
    };
    constexpr std::array invalid{
        Invalid{R"({})", "version"},
        Invalid{R"({"version":2})", "version"},
        Invalid{R"({"version":-1})", "version"},
        Invalid{R"({"version":1.0})", "version"},
        Invalid{R"({"version":"1"})", "version"},
        Invalid{R"({"version":true})", "version"},
        Invalid{R"({"version":18446744073709551615})", "version"},
        Invalid{R"({"version":1,"colorSpce":"linear"})", "colorSpce"},
        Invalid{R"({"version":1,"usage":"infer"})", "usage"},
        Invalid{R"({"version":1,"usage":null})", "usage"},
        Invalid{R"({"version":1,"format":"bc6h"})", "format"},
        Invalid{R"({"version":1,"format":7})", "format"},
        Invalid{R"({"version":1,"colorSpace":"gamma"})", "colorSpace"},
        Invalid{R"({"version":1,"mipmaps":1})", "mipmaps"},
        Invalid{R"({"version":1,"flipNormalY":"true"})", "flipNormalY"},
        Invalid{R"({"version":1,"alphaMode":"blend"})", "alphaMode"},
        Invalid{R"({"version":1,"alphaMode":false})", "alphaMode"},
        Invalid{R"({"version":1,"alphaCutoff":"0.5"})", "alphaCutoff"},
        Invalid{R"({"version":1,"alphaCutoff":false})", "alphaCutoff"},
        Invalid{R"({"version":1,"alphaCutoff":-0.01})", "alphaCutoff"},
        Invalid{R"({"version":1,"alphaCutoff":1.01})", "alphaCutoff"},
        Invalid{R"({"version":1,"alphaCutoff":1e309})", "alphaCutoff"},
        Invalid{R"({"version":1,"usage":"normal","usage":"color"})", "usage"},
        Invalid{R"({"version":1,"faces":{}})", "faces"}};
    for (const auto& item : invalid)
    {
        INFO(item.Json);
        std::string error;
        CHECK_FALSE(ParseTextureSettings(item.Json, kSettingsPath, error));
        CHECK(Contains(error, kSettingsPath));
        CHECK(Contains(error, item.Field));
    }
    for (const auto text : {"", "not JSON", "[]", "null", "1", "{\"version\":", "{\"version\":1,}"})
    {
        INFO(text);
        std::string error;
        CHECK_FALSE(ParseTextureSettings(text, kSettingsPath, error));
        CHECK(Contains(error, kSettingsPath));
    }
}

TEST_CASE("Recognized texture fields use defaults and canonical tokens",
          "[asset][texture-settings]")
{
    SettingsFixture fixture;
    fixture.Saved = fixture.Parse(R"({"version":1})");
    const auto resolved = fixture.Resolve();
    REQUIRE(resolved);
    CHECK(resolved->Authored.Usage == TextureUsage::Color);
    CHECK(resolved->Authored.Format == TextureCompression::Auto);
    CHECK(resolved->Authored.ColorSpace == TextureColorSpace::Auto);
    CHECK(resolved->Authored.bMipmaps);
    CHECK_FALSE(resolved->Authored.bFlipNormalY);
    CHECK(resolved->AlphaMode == TextureAlphaMode::Opaque);
    CHECK(resolved->Authored.AlphaCutoff == 0.5);
    CHECK(resolved->Format == TextureCompression::BC7);
    CHECK(resolved->ColorSpace == TextureColorSpace::Srgb);
    CHECK(resolved->bAlphaUsageKnown);

    const auto authored = fixture.Parse(R"({"version":1,"usage":"NORMAL","format":"BC7",
        "colorSpace":"sRGB","mipmaps":false,"flipNormalY":true,"alphaMode":"opaque",
        "alphaCutoff":0.25})");
    CHECK(authored.Usage == TextureUsage::Normal);
    CHECK(authored.Format == TextureCompression::BC7);
    CHECK(authored.ColorSpace == TextureColorSpace::Srgb);
    const auto text = SerializeTextureSettings(authored, kSettingsPath, fixture.Error);
    REQUIRE(text);
    CHECK(Contains(*text, "\"normal\""));
    CHECK(Contains(*text, "\"bc7\""));
    CHECK(Contains(*text, "\"srgb\""));
    CHECK(fixture.Parse(*text) == authored);
    CHECK_FALSE(ParseTextureUsage("color "));
    CHECK_FALSE(ParseTextureCompression("bc8"));
    CHECK_FALSE(ParseTextureColorSpace("2.2"));
    CHECK_FALSE(ParseTextureAlphaMode("auto"));
}

TEST_CASE("Every texture usage resolves its documented automatic defaults",
          "[asset][texture-settings]")
{
    struct Default
    {
        TextureUsage Usage;
        TextureCompression Format;
        TextureColorSpace ColorSpace;
    };
    constexpr std::array defaults{
        Default{TextureUsage::Color, TextureCompression::BC7, TextureColorSpace::Srgb},
        Default{TextureUsage::Normal, TextureCompression::BC5, TextureColorSpace::Linear},
        Default{TextureUsage::Scalar, TextureCompression::BC4, TextureColorSpace::Linear},
        Default{TextureUsage::Packed, TextureCompression::BC7, TextureColorSpace::Linear}};
    for (const auto& expected : defaults)
    {
        SettingsFixture fixture;
        fixture.Overrides.Usage = expected.Usage;
        const auto resolved = fixture.Resolve();
        REQUIRE(resolved);
        CHECK(resolved->Authored.Usage == expected.Usage);
        CHECK(resolved->Format == expected.Format);
        CHECK(resolved->ColorSpace == expected.ColorSpace);
        CHECK(resolved->Authored.Format == TextureCompression::Auto);
        CHECK(resolved->Authored.ColorSpace == TextureColorSpace::Auto);
    }
}

TEST_CASE("Usage changes resolve auto fields and preserve explicit texture choices",
          "[asset][texture-settings]")
{
    SettingsFixture fixture;
    fixture.Overrides.Usage = TextureUsage::Normal;
    SECTION("auto fields follow the new usage after serialization")
    {
        const auto normal = fixture.Resolve();
        REQUIRE(normal);
        fixture.Reload(normal->Authored);
        fixture.Overrides.Usage = TextureUsage::Scalar;
        const auto scalar = fixture.Resolve();
        REQUIRE(scalar);
        CHECK(scalar->Format == TextureCompression::BC4);
        CHECK(scalar->ColorSpace == TextureColorSpace::Linear);
    }
    SECTION("explicit BC7 and linear stay explicit")
    {
        fixture.Saved.Format = TextureCompression::BC7;
        fixture.Saved.ColorSpace = TextureColorSpace::Linear;
        const auto normal = fixture.Resolve();
        REQUIRE(normal);
        CHECK(normal->Format == TextureCompression::BC7);
        CHECK(normal->ColorSpace == TextureColorSpace::Linear);
        CHECK(normal->Authored.Format == TextureCompression::BC7);
        CHECK(normal->Authored.ColorSpace == TextureColorSpace::Linear);
    }
    SECTION("an incompatible saved explicit colour space fails")
    {
        fixture.Saved.Format = TextureCompression::BC7;
        fixture.Saved.ColorSpace = TextureColorSpace::Srgb;
        CHECK_FALSE(fixture.Resolve());
        CHECK(Contains(fixture.Error, "colorSpace"));
        CHECK(fixture.Saved.ColorSpace == TextureColorSpace::Srgb);
    }
}

TEST_CASE("Compression overrides do not establish normal usage", "[asset][texture-settings]")
{
    SettingsFixture fixture;
    fixture.Overrides.Format = TextureCompression::BC5;
    CHECK_FALSE(fixture.Resolve());
    CHECK(Contains(fixture.Error, "no sRGB variants"));
    fixture.Overrides.ColorSpace = TextureColorSpace::Linear;
    auto resolved = fixture.Resolve();
    REQUIRE(resolved);
    CHECK(resolved->Authored.Usage == TextureUsage::Color);
    fixture.Overrides.bFlipNormalY = true;
    CHECK_FALSE(fixture.Resolve());
    CHECK(Contains(fixture.Error, "flipNormalY"));

    fixture.Saved.Usage = TextureUsage::Normal;
    resolved = fixture.Resolve();
    REQUIRE(resolved);
    CHECK(resolved->Authored.Usage == TextureUsage::Normal);
    CHECK(resolved->Authored.bFlipNormalY);
}

TEST_CASE("Omitted texture overrides retain saved values and flags never toggle",
          "[asset][texture-settings]")
{
    SettingsFixture fixture;
    fixture.Overrides.Usage = TextureUsage::Normal;
    fixture.Overrides.bFlipNormalY = true;
    fixture.Overrides.bNoMips = true;
    const auto first = fixture.Resolve();
    REQUIRE(first);
    fixture.Reload(first->Authored);
    const auto omitted = fixture.Resolve();
    REQUIRE(omitted);
    CHECK(omitted->Authored == first->Authored);
    CHECK(omitted->Authored.bFlipNormalY);
    CHECK_FALSE(omitted->Authored.bMipmaps);
    fixture.Overrides.bFlipNormalY = true;
    fixture.Overrides.bNoMips = true;
    const auto repeated = fixture.Resolve();
    REQUIRE(repeated);
    CHECK(repeated->Authored == omitted->Authored);

    fixture.Overrides = {};
    fixture.Saved = fixture.Parse(R"({"version":1,"usage":"normal","flipNormalY":false})");
    const auto edited = fixture.Resolve();
    REQUIRE(edited);
    CHECK_FALSE(edited->Authored.bFlipNormalY);
    CHECK(edited->Authored.bMipmaps);
}

TEST_CASE("Normal settings require compatible channels and colour interpretation",
          "[asset][texture-settings]")
{
    SettingsFixture fixture;
    fixture.Saved.Usage = TextureUsage::Normal;
    SECTION("sRGB normals")
    {
        fixture.Saved.Format = TextureCompression::BC7;
        fixture.Saved.ColorSpace = TextureColorSpace::Srgb;
        CHECK_FALSE(fixture.Resolve());
        CHECK(Contains(fixture.Error, "Normal usage requires linear"));
    }
    SECTION("single-channel storage")
    {
        fixture.Saved.Format = TextureCompression::BC4;
        CHECK_FALSE(fixture.Resolve());
        CHECK(Contains(fixture.Error, "at least two stored channels"));
    }
    SECTION("grayscale and grayscale-alpha originals")
    {
        for (uint32_t channels : {1u, 2u})
        {
            fixture.Source.Channels = channels;
            CHECK_FALSE(fixture.Resolve());
            CHECK(Contains(fixture.Error, "RGB/XYZ"));
        }
    }
    SECTION("RGB and RGBA originals")
    {
        for (uint32_t channels : {3u, 4u})
        {
            fixture.Source.Channels = channels;
            CHECK(fixture.Resolve());
        }
    }
}

TEST_CASE("BC4 and BC5 reject sRGB even under explicit usage and format overrides",
          "[asset][texture-settings]")
{
    for (const auto format : {TextureCompression::BC4, TextureCompression::BC4Signed,
                              TextureCompression::BC5, TextureCompression::BC5Signed})
    {
        SettingsFixture fixture;
        fixture.Overrides.Format = format;
        fixture.Overrides.ColorSpace = TextureColorSpace::Srgb;
        CHECK_FALSE(fixture.Resolve());
        CHECK(Contains(fixture.Error, "colorSpace"));
        CHECK(Contains(fixture.Error, "BC4/BC5"));
    }
}

TEST_CASE("Internal DDS encoders are not offered for new image inputs", "[asset][texture-settings]")
{
    for (const auto format : {TextureCompression::BC2, TextureCompression::BC3,
                              TextureCompression::BC4Signed, TextureCompression::BC5Signed})
    {
        SettingsFixture fixture;
        fixture.Overrides.Format = format;
        fixture.Overrides.ColorSpace = TextureColorSpace::Linear;
        CHECK_FALSE(fixture.Resolve());
        CHECK(Contains(fixture.Error, "compressed DDS input"));
        fixture.Source.bDds = true;
        CHECK_FALSE(fixture.Resolve());
        fixture.Source.NativeFormat = TextureCompression::BC7;
        CHECK(fixture.Resolve());
    }
}

TEST_CASE("Alpha cutoffs accept endpoints and reject nonfinite or out-of-range overrides",
          "[asset][texture-settings]")
{
    SettingsFixture fixture;
    for (double cutoff : {0.0, 0.25, 0.5, 1.0})
    {
        fixture.Overrides.AlphaCutoff = cutoff;
        const auto resolved = fixture.Resolve();
        REQUIRE(resolved);
        fixture.Reload(resolved->Authored);
        CHECK(fixture.Saved.AlphaCutoff == cutoff);
    }
    for (double cutoff : {-0.001, 1.001, std::numeric_limits<double>::infinity(),
                          std::numeric_limits<double>::quiet_NaN()})
    {
        fixture.Overrides.AlphaCutoff = cutoff;
        CHECK_FALSE(fixture.Resolve());
        CHECK(Contains(fixture.Error, "alphaCutoff"));
    }
}

TEST_CASE("Masked images require alpha storage and no generated mips", "[asset][texture-settings]")
{
    SettingsFixture fixture;
    fixture.Overrides.AlphaMode = TextureAlphaMode::Mask;
    CHECK_FALSE(fixture.Resolve());
    CHECK(Contains(fixture.Error, "--no-mips"));
    fixture.Overrides.bNoMips = true;
    for (const auto format : {TextureCompression::BC1, TextureCompression::BC7})
    {
        fixture.Overrides.Format = format;
        const auto masked = fixture.Resolve();
        REQUIRE(masked);
        CHECK(masked->AlphaMode == TextureAlphaMode::Mask);
        CHECK(masked->Authored.AlphaCutoff == 0.5);
        CHECK_FALSE(masked->Authored.bMipmaps);
        fixture.Reload(masked->Authored);
        CHECK(fixture.Resolve());
    }
    fixture.Overrides.Format = TextureCompression::BC4;
    fixture.Overrides.ColorSpace = TextureColorSpace::Linear;
    CHECK_FALSE(fixture.Resolve());
    CHECK(Contains(fixture.Error, "alpha-capable"));
}

TEST_CASE("Supplied masked DDS chains are valid without authorizing new masked levels",
          "[asset][texture-settings]")
{
    SettingsFixture fixture;
    fixture.Source.bDds = true;
    fixture.Source.NativeFormat = TextureCompression::BC3;
    fixture.Source.NativeColorSpace = TextureColorSpace::Srgb;
    fixture.Source.NativeAlphaMode = TextureAlphaMode::Mask;
    fixture.Source.bNeedsMipGeneration = false;
    const auto retained = fixture.Resolve();
    REQUIRE(retained);
    CHECK(retained->Authored.bMipmaps);
    CHECK(retained->AlphaMode == TextureAlphaMode::Mask);
    CHECK(retained->bAlphaUsageKnown);
    CHECK_FALSE(retained->bRecompressExistingMips);
    fixture.Reload(retained->Authored);
    CHECK_FALSE(fixture.Saved.AlphaMode);
    CHECK(fixture.Resolve());
    fixture.Source.bNeedsMipGeneration = true;
    CHECK_FALSE(fixture.Resolve());
    CHECK(Contains(fixture.Error, "--no-mips"));
    fixture.Overrides.bNoMips = true;
    CHECK(fixture.Resolve());
}

TEST_CASE("DDS automatic storage preserves native formats and explicit colour metadata",
          "[asset][texture-settings]")
{
    for (const auto format :
         {TextureCompression::BC1, TextureCompression::BC2, TextureCompression::BC3,
          TextureCompression::BC4, TextureCompression::BC4Signed, TextureCompression::BC5,
          TextureCompression::BC5Signed, TextureCompression::BC7})
    {
        SettingsFixture fixture;
        fixture.Source.bDds = true;
        fixture.Source.NativeFormat = format;
        fixture.Source.NativeColorSpace = TextureColorSpace::Linear;
        fixture.Source.bNeedsMipGeneration = false;
        const auto retained = fixture.Resolve();
        REQUIRE(retained);
        CHECK(retained->Format == format);
        CHECK(retained->ColorSpace == TextureColorSpace::Linear);
        CHECK_FALSE(retained->bRecompressExistingMips);
        CHECK(retained->Authored.Format == TextureCompression::Auto);
    }
}

TEST_CASE("Legacy DDS colour assumptions follow usage and warn with recooking guidance",
          "[asset][texture-settings]")
{
    SettingsFixture fixture;
    fixture.Source.bDds = true;
    fixture.Source.NativeFormat = TextureCompression::BC7;
    const auto color = fixture.Resolve();
    REQUIRE(color);
    CHECK(color->ColorSpace == TextureColorSpace::Srgb);
    CHECK(fixture.WarnsAbout("no explicit colour-space metadata"));
    fixture.Overrides.Usage = TextureUsage::Normal;
    const auto normal = fixture.Resolve();
    REQUIRE(normal);
    CHECK(normal->Format == TextureCompression::BC7);
    CHECK(normal->ColorSpace == TextureColorSpace::Linear);
    CHECK_FALSE(normal->bRecompressExistingMips);
    for (const auto& warning : fixture.Warnings)
    {
        CHECK(Contains(warning, kSettingsPath));
        CHECK(Contains(warning, "settings file"));
        CHECK(Contains(warning, "recook"));
    }
    fixture.Overrides.ColorSpace = TextureColorSpace::Linear;
    CHECK(fixture.Resolve());
    CHECK_FALSE(fixture.WarnsAbout("no explicit colour-space metadata"));
}

TEST_CASE("DDS colour reinterpretation preserves blocks while explicit pixel changes warn",
          "[asset][texture-settings]")
{
    SettingsFixture fixture;
    fixture.Source.bDds = true;
    fixture.Source.NativeFormat = TextureCompression::BC7;
    fixture.Source.NativeColorSpace = TextureColorSpace::Srgb;
    fixture.Source.NativeAlphaMode = TextureAlphaMode::Opaque;
    fixture.Overrides.ColorSpace = TextureColorSpace::Linear;
    auto resolved = fixture.Resolve();
    REQUIRE(resolved);
    CHECK_FALSE(resolved->bRecompressExistingMips);
    CHECK_FALSE(fixture.WarnsAbout("recompress"));

    fixture.Overrides.Format = TextureCompression::BC1;
    resolved = fixture.Resolve();
    REQUIRE(resolved);
    CHECK(resolved->bRecompressExistingMips);
    CHECK(fixture.WarnsAbout("byte preservation no longer applies"));
    fixture.Reload(resolved->Authored);
    resolved = fixture.Resolve();
    REQUIRE(resolved);
    CHECK(resolved->bRecompressExistingMips);

    fixture.Overrides.Format = TextureCompression::Auto;
    fixture.Overrides.Usage = TextureUsage::Normal;
    fixture.Overrides.bFlipNormalY = true;
    resolved = fixture.Resolve();
    REQUIRE(resolved);
    CHECK(resolved->Format == TextureCompression::BC7);
    CHECK(resolved->bRecompressExistingMips);
}

TEST_CASE("Signed BC5 normals retain signed storage without an explicit conversion",
          "[asset][texture-settings]")
{
    SettingsFixture fixture;
    fixture.Saved.Usage = TextureUsage::Normal;
    fixture.Source.bDds = true;
    fixture.Source.Channels = 2;
    fixture.Source.NativeFormat = TextureCompression::BC5Signed;
    fixture.Source.NativeColorSpace = TextureColorSpace::Linear;
    const auto normal = fixture.Resolve();
    REQUIRE(normal);
    CHECK(normal->Format == TextureCompression::BC5Signed);
    CHECK_FALSE(normal->bRecompressExistingMips);
    CHECK_FALSE(fixture.WarnsAbout("alpha channel"));
    fixture.Overrides.Format = TextureCompression::BC5;
    const auto converted = fixture.Resolve();
    REQUIRE(converted);
    CHECK(converted->Format == TextureCompression::BC5);
    CHECK(converted->bRecompressExistingMips);
}

TEST_CASE("Retained DDS blocks do not produce pixel-loss warnings", "[asset][texture-settings]")
{
    SettingsFixture fixture;
    fixture.Source.bDds = true;
    fixture.Source.NativeFormat = TextureCompression::BC1;
    fixture.Source.NativeColorSpace = TextureColorSpace::Srgb;
    fixture.Source.NativeAlphaMode = TextureAlphaMode::Opaque;
    fixture.Source.Channels = 4;
    fixture.Source.bNeedsMipGeneration = false;
    const auto retained = fixture.Resolve();
    REQUIRE(retained);
    CHECK_FALSE(retained->bRecompressExistingMips);
    CHECK_FALSE(fixture.WarnsAbout("discards"));
    fixture.Source.NativeFormat = TextureCompression::BC4;
    fixture.Source.NativeColorSpace = TextureColorSpace::Linear;
    fixture.Saved.Usage = TextureUsage::Normal;
    fixture.Overrides.Format = TextureCompression::BC5;
    CHECK_FALSE(fixture.Resolve());
    CHECK(Contains(fixture.Error, "two native channels"));
}

TEST_CASE("Unspecified DDS alpha use survives serialization and warns only for generated levels",
          "[asset][texture-settings]")
{
    SettingsFixture fixture;
    fixture.Source.bDds = true;
    fixture.Source.NativeFormat = TextureCompression::BC7;
    fixture.Source.NativeColorSpace = TextureColorSpace::Linear;
    const auto unresolved = fixture.Resolve();
    REQUIRE(unresolved);
    CHECK(unresolved->AlphaMode == TextureAlphaMode::Opaque);
    CHECK_FALSE(unresolved->bAlphaUsageKnown);
    CHECK(fixture.WarnsAbout("does not preserve alpha-test coverage"));
    CHECK(fixture.WarnsAbout("--no-mips"));
    fixture.Reload(unresolved->Authored);
    CHECK_FALSE(fixture.Saved.AlphaMode);
    CHECK(fixture.Resolve());
    CHECK(fixture.WarnsAbout("alpha usage is unresolved"));
    fixture.Source.bNeedsMipGeneration = false;
    CHECK(fixture.Resolve());
    CHECK_FALSE(fixture.WarnsAbout("alpha usage is unresolved"));
    fixture.Source.bNeedsMipGeneration = true;
    fixture.Overrides.AlphaMode = TextureAlphaMode::Opaque;
    const auto explicitOpaque = fixture.Resolve();
    REQUIRE(explicitOpaque);
    CHECK(explicitOpaque->bAlphaUsageKnown);
    CHECK_FALSE(fixture.WarnsAbout("alpha usage is unresolved"));
}

TEST_CASE("Suspicious source settings warn without inferring usage", "[asset][texture-settings]")
{
    SettingsFixture fixture;
    fixture.Source.bSettingsMissing = true;
    fixture.Source.Channels = 1;
    auto resolved = fixture.Resolve();
    REQUIRE(resolved);
    CHECK(resolved->Authored.Usage == TextureUsage::Color);
    CHECK(fixture.WarnsAbout("assuming color"));
    CHECK(fixture.WarnsAbout("scalar/grayscale"));
    fixture.Overrides.Usage = TextureUsage::Scalar;
    fixture.Source.Channels = 2;
    resolved = fixture.Resolve();
    REQUIRE(resolved);
    CHECK_FALSE(fixture.WarnsAbout("assuming color"));
    CHECK(fixture.WarnsAbout("discards the source alpha"));
    fixture.Source.Channels = 3;
    CHECK(fixture.Resolve());
    CHECK(fixture.WarnsAbout("remaining source colour/data channels will be discarded"));
    fixture.Overrides = {};
    fixture.Overrides.Format = TextureCompression::BC1;
    fixture.Source.Channels = 4;
    CHECK(fixture.Resolve());
    CHECK(fixture.WarnsAbout("discards the source alpha"));
}

TEST_CASE("Batch settings reject all per-texture overrides including explicit zero values",
          "[asset][texture-settings]")
{
    std::array<TextureSettingsOverrides, 7> overrides{};
    overrides[0].Usage = TextureUsage::Color;
    overrides[1].Format = TextureCompression::Auto;
    overrides[2].ColorSpace = TextureColorSpace::Auto;
    overrides[3].AlphaMode = TextureAlphaMode::Opaque;
    overrides[4].AlphaCutoff = 0.0;
    overrides[5].bFlipNormalY = true;
    overrides[6].bNoMips = true;
    SettingsFixture fixture;
    fixture.Saved.Usage = TextureUsage::Normal;
    fixture.Saved.bFlipNormalY = true;
    CHECK(fixture.Resolve(true));
    for (const auto& item : overrides)
    {
        fixture.Overrides = item;
        CHECK_FALSE(fixture.Resolve(true));
        CHECK(Contains(fixture.Error, "cook-all"));
        CHECK(Contains(fixture.Error, "single-file cook"));
    }
}

TEST_CASE("Failed texture resolution preserves saved settings and produces no stale warnings",
          "[asset][texture-settings]")
{
    SettingsFixture fixture;
    fixture.Saved.Usage = TextureUsage::Normal;
    fixture.Saved.bFlipNormalY = true;
    fixture.Saved.AlphaCutoff = 0.75;
    const TextureSettings original = fixture.Saved;
    fixture.Overrides.Usage = TextureUsage::Color;
    fixture.Warnings.push_back("old warning");
    CHECK_FALSE(fixture.Resolve());
    CHECK(Contains(fixture.Error, kSettingsPath));
    CHECK(fixture.Saved == original);
    CHECK(fixture.Warnings.empty());
    fixture.Overrides = {};
    CHECK(fixture.Resolve());
    CHECK(fixture.Error.empty());
}

TEST_CASE("Invalid programmatic texture values cannot be serialized or resolved",
          "[asset][texture-settings]")
{
    SettingsFixture fixture;
    SECTION("future schema")
    {
        fixture.Saved.Version = 2;
    }
    SECTION("unknown usage")
    {
        fixture.Saved.Usage = static_cast<TextureUsage>(255);
    }
    SECTION("unknown compression")
    {
        fixture.Saved.Format = static_cast<TextureCompression>(255);
    }
    SECTION("unknown colour space")
    {
        fixture.Saved.ColorSpace = static_cast<TextureColorSpace>(255);
    }
    SECTION("unknown alpha mode")
    {
        fixture.Saved.AlphaMode = static_cast<TextureAlphaMode>(255);
    }
    SECTION("nonfinite cutoff")
    {
        fixture.Saved.AlphaCutoff = std::numeric_limits<double>::quiet_NaN();
    }
    CHECK_FALSE(SerializeTextureSettings(fixture.Saved, kSettingsPath, fixture.Error));
    CHECK(Contains(fixture.Error, kSettingsPath));
    CHECK_FALSE(fixture.Resolve());
}

TEST_CASE("RGBA expansion retains original source channel counts for settings validation",
          "[asset][texture-settings]")
{
    constexpr std::array names{"channels-gray.png", "channels-gray-alpha.png", "channels-rgb.png",
                               "channels-rgba.png"};
    for (size_t index = 0; index < names.size(); ++index)
    {
        const auto image = ReadPng(std::string(HIKARI_TEXTURE_TEST_DATA_DIR) + names[index]);
        REQUIRE(image);
        CHECK(image->SourceChannels == index + 1);
        CHECK(image->Extent.Width == 2);
        CHECK(image->Extent.Height == 1);
        CHECK(image->Pixels.size() == 8);
    }
}
