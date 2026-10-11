#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <asset/CubemapDescriptor.h>
#include <asset/TextureCooker.h>

#include "TextureCookFixture.h"

using namespace Hikari::Asset;
namespace fs = std::filesystem;

namespace
{
bool Contains(std::string_view text, std::string_view part)
{
    return text.find(part) != std::string_view::npos;
}

std::string Replace(std::string text, std::string_view from, std::string_view to)
{
    const size_t position = text.find(from);
    REQUIRE(position != std::string::npos);
    text.replace(position, from.size(), to);
    return text;
}
} // namespace

TEST_CASE("Cook paths keep source extensions and use a content-relative cooked sibling",
          "[asset][texture-cooker]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("textures/brick.png");
    fs::copy_file(HIKARI_TEXTURE_TEST_DATA_DIR "constant.jpg",
                  fixture.Source("textures/brick.jpg"));
    TextureCookFixture::WriteText(fixture.Source("textures/brick.dds"), "DDS placeholder");
    std::string error;
    for (const auto extension : {"png", "jpg", "dds"})
    {
        const std::string source = std::string("textures/brick.") + extension;
        const auto paths = MapTextureCookPaths(fixture.Content(), {}, source, error);
        INFO(error);
        REQUIRE(paths);
        CHECK(paths->SourceRelative == fs::path(source));
        CHECK(paths->SettingsPath == fixture.Source(source + ".json"));
        CHECK(paths->CookedPath == fixture.Cooked() / (source + ".ktx2"));
        CHECK(paths->RecordPath == fixture.Cooked() / (source + ".cook.json"));
    }
    const auto custom = fixture.Root() / "custom cooked";
    const auto paths =
        MapTextureCookPaths(fixture.Content(), custom, fixture.Source("textures/brick.png"), error);
    REQUIRE(paths);
    CHECK(paths->CookedPath == custom / "textures/brick.png.ktx2");
    CHECK_FALSE(fs::exists(custom));
    CHECK_FALSE(fs::exists(fixture.Cooked()));
}

TEST_CASE("Cook paths reject missing inputs, unsupported types and sources outside roots",
          "[asset][texture-cooker]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("../outside.png");
    TextureCookFixture::WriteText(fixture.Source("readme.txt"), "text");
    std::string error;
    for (const fs::path& source :
         {fs::path("missing.png"), fs::path("../outside.png"), fixture.Root() / "outside.png",
          fs::path("readme.txt"), fs::path(".")})
        CHECK_FALSE(MapTextureCookPaths(fixture.Content(), {}, source, error));
    CHECK_FALSE(MapTextureCookPaths(fixture.Root() / "missing", {}, "missing.png", error));
    CHECK(Contains(error, "source-root"));
    fixture.WriteImage("generated/image.png");
    CHECK_FALSE(MapTextureCookPaths(fixture.Content(), fixture.Source("generated"),
                                    "generated/image.png", error));
    CHECK(Contains(error, "cannot be source inputs"));
    CHECK_FALSE(fs::exists(fixture.Cooked()));
}

TEST_CASE("Cook path symlinks cannot escape source or generated roots", "[asset][texture-cooker]")
{
    TextureCookFixture fixture;
    const auto outside = fixture.WriteImage("../outside.png");
    fixture.WriteImage("nested/image.png");
    std::string error;
    std::error_code ec;
    SECTION("source")
    {
        fs::create_symlink(outside, fixture.Source("linked.png"), ec);
        if (ec)
            SKIP("Symlink creation unavailable: " + ec.message());
        CHECK_FALSE(MapTextureCookPaths(fixture.Content(), {}, "linked.png", error));
        CHECK(Contains(error, "outside source root"));
    }
    SECTION("settings")
    {
        TextureCookFixture::WriteText(fixture.Root() / "outside.json", R"({"version":1})");
        fs::create_symlink(fixture.Root() / "outside.json", fixture.Source("nested/image.png.json"),
                           ec);
        if (ec)
            SKIP("Symlink creation unavailable: " + ec.message());
        CHECK_FALSE(MapTextureCookPaths(fixture.Content(), {}, "nested/image.png", error));
        CHECK(Contains(error, "escapes"));
    }
    SECTION("output")
    {
        fs::create_directory(fixture.Cooked());
        fs::create_directory(fixture.Root() / "outside-output");
        fs::create_directory_symlink(fixture.Root() / "outside-output", fixture.Cooked() / "nested",
                                     ec);
        if (ec)
            SKIP("Symlink creation unavailable: " + ec.message());
        CHECK_FALSE(MapTextureCookPaths(fixture.Content(), {}, "nested/image.png", error));
        CHECK(Contains(error, "escapes"));
    }
}

TEST_CASE("Preparing is read-only and saving defaults creates only source settings",
          "[asset][texture-cooker]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("image.png");
    const auto cooker = fixture.Cooker();
    std::string error;
    std::vector<std::string> warnings;
    const auto prepared = cooker.Prepare({.Source = "image.png"}, error, warnings);
    INFO(error);
    REQUIRE(prepared);
    CHECK(prepared->Settings.Format == TextureCompression::BC7);
    CHECK(prepared->Settings.ColorSpace == TextureColorSpace::Srgb);
    CHECK(prepared->ImageMetadata.front().Channels == 4);
    CHECK_FALSE(warnings.empty());
    CHECK_FALSE(fs::exists(prepared->Paths.SettingsPath));
    REQUIRE(cooker.SaveSettings(*prepared, error));
    const auto saved = ParseTextureSettings(
        TextureCookFixture::ReadText(prepared->Paths.SettingsPath), "image.png.json", error);
    REQUIRE(saved);
    CHECK(saved->Format == TextureCompression::Auto);
    CHECK(saved->ColorSpace == TextureColorSpace::Auto);
    CHECK(saved->AlphaMode == TextureAlphaMode::Opaque);
    CHECK_FALSE(fs::exists(fixture.Cooked()));
}

TEST_CASE("Persisted overrides retain flips and omitted options preserve JSON bytes",
          "[asset][texture-cooker]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("normal.png");
    const auto cooker = fixture.Cooker();
    TextureCookRequest request{.Source = "normal.png"};
    request.Overrides.Usage = TextureUsage::Normal;
    request.Overrides.bFlipNormalY = true;
    request.Overrides.bNoMips = true;
    std::string error;
    std::vector<std::string> warnings;
    auto prepared = cooker.Prepare(request, error, warnings);
    REQUIRE(prepared);
    REQUIRE(cooker.SaveSettings(*prepared, error));
    const auto json = TextureCookFixture::ReadText(prepared->Paths.SettingsPath);
    request.Overrides = {};
    request.bForce = true;
    prepared = cooker.Prepare(request, error, warnings);
    REQUIRE(prepared);
    CHECK(prepared->Settings.Authored.Usage == TextureUsage::Normal);
    CHECK(prepared->Settings.Authored.bFlipNormalY);
    CHECK_FALSE(prepared->Settings.Authored.bMipmaps);
    CHECK_FALSE(prepared->bWriteSettings);
    REQUIRE(cooker.SaveSettings(*prepared, error));
    CHECK(TextureCookFixture::ReadText(prepared->Paths.SettingsPath) == json);
    request.Overrides.bFlipNormalY = true;
    prepared = cooker.Prepare(request, error, warnings);
    REQUIRE(prepared);
    REQUIRE(cooker.SaveSettings(*prepared, error));
    CHECK(TextureCookFixture::ReadText(prepared->Paths.SettingsPath) == json);
}

TEST_CASE("Invalid cooking requests preserve existing settings, outputs and records",
          "[asset][texture-cooker]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("image.png");
    const std::string settings = R"({"version":1,"usage":"normal","flipNormalY":true})";
    TextureCookFixture::WriteText(fixture.Source("image.png.json"), settings);
    TextureCookFixture::WriteText(fixture.Cooked() / "image.png.ktx2", "last asset");
    TextureCookFixture::WriteText(fixture.Cooked() / "image.png.cook.json", "old record");
    TextureCookRequest request{.Source = "image.png"};
    request.Overrides.Usage = TextureUsage::Color;
    const auto result = fixture.Cooker().Cook(request);
    CHECK(result.Status == TextureCookStatus::Failed);
    CHECK(Contains(result.Error, "flipNormalY"));
    CHECK(TextureCookFixture::ReadText(fixture.Source("image.png.json")) == settings);
    CHECK(TextureCookFixture::ReadText(fixture.Cooked() / "image.png.ktx2") == "last asset");
    CHECK(TextureCookFixture::ReadText(fixture.Cooked() / "image.png.cook.json") == "old record");
    CHECK(result.Warnings.empty());
}

TEST_CASE("Saving prepared settings rejects intervening author edits", "[asset][texture-cooker]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("image.png");
    const auto cooker = fixture.Cooker();
    std::string error;
    std::vector<std::string> warnings;
    auto prepared = cooker.Prepare({.Source = "image.png"}, error, warnings);
    REQUIRE(prepared);
    const std::string edited = R"({"version":1,"usage":"scalar"})";
    TextureCookFixture::WriteText(fixture.Source("image.png.json"), edited);
    CHECK_FALSE(cooker.SaveSettings(*prepared, error));
    CHECK(Contains(error, "changed since preparation"));
    CHECK(TextureCookFixture::ReadText(fixture.Source("image.png.json")) == edited);
    TextureCookRequest request{.Source = "image.png"};
    request.Overrides.Usage = TextureUsage::Packed;
    prepared = cooker.Prepare(request, error, warnings);
    REQUIRE(prepared);
    TextureCookFixture::WriteText(fixture.Source("image.png.json"), R"({"version":1})");
    CHECK_FALSE(cooker.SaveSettings(*prepared, error));
}

TEST_CASE("Cooker defaults cannot silently convert DDS before reading native metadata",
          "[asset][texture-cooker]")
{
    TextureCookFixture fixture;
    TextureCookFixture::WriteText(fixture.Source("native.dds"), "DDS placeholder");
    const auto result = fixture.Cooker().Cook({.Source = "native.dds"});
    CHECK(result.Status == TextureCookStatus::Failed);
    CHECK(Contains(result.Error, "DDS header: Truncated header"));
    CHECK_FALSE(fs::exists(fixture.Source("native.dds.json")));
    CHECK_FALSE(fs::exists(fixture.Cooked()));
}

TEST_CASE("Cubemap descriptors strictly require six explicit relative face paths",
          "[asset][texture-cooker]")
{
    TextureCookFixture fixture;
    fixture.WriteCubemap();
    const auto json = TextureCookFixture::ReadText(fixture.Source("sky/sky.cubemap.json"));
    std::string error;
    const auto parsed = ParseCubemapDescriptor(json, "sky.cubemap.json", error);
    REQUIRE(parsed);
    const std::array names{"right.png",  "left.png", "top.png",
                           "bottom.png", "back.png", "front.png"};
    for (size_t i = 0; i < names.size(); ++i)
        CHECK(parsed->Faces[i] == fs::path(names[i]));
    const auto serialized = SerializeCubemapDescriptor(*parsed, "sky.cubemap.json", error);
    REQUIRE(serialized);
    const auto reopened = ParseCubemapDescriptor(*serialized, "sky.cubemap.json", error);
    REQUIRE(reopened);
    CHECK(reopened->Faces == parsed->Faces);
    for (const auto& invalid :
         {std::string(R"({"version":1})"), Replace(json, "\"negativeZ\":\"front.png\",", ""),
          Replace(json, "negativeZ", "back"), Replace(json, "\"front.png\"", "42"),
          Replace(json, "front.png", R"(front\u0000.png)"), Replace(json, "\"front.png\"", "\"\""),
          Replace(json, "\"version\":1", "\"version\":2"),
          Replace(json, "\"version\":1", "\"version\":1,\"typo\":true"),
          Replace(json, "\"negativeZ\":\"front.png\"",
                  "\"negativeZ\":\"front.png\",\"negativeZ\":\"other.png\"")})
    {
        CHECK_FALSE(ParseCubemapDescriptor(invalid, "sky.cubemap.json", error));
        CHECK(Contains(error, "sky.cubemap.json"));
        CHECK(Contains(error, "Field"));
    }
    const auto absolute =
        Replace(json, "front.png", (fixture.Root() / "front.png").generic_string());
    CHECK_FALSE(ParseCubemapDescriptor(absolute, "sky.cubemap.json", error));
}

TEST_CASE("Cubemap preparation maps one identity and preserves explicit face order",
          "[asset][texture-cooker]")
{
    TextureCookFixture fixture;
    fixture.WriteCubemap(R"("usage":"packed",)");
    TextureCookFixture::WriteText(fixture.Source("sky/right.png.json"), R"({"version":99})");
    std::string error;
    std::vector<std::string> warnings;
    TextureCookRequest request{.Source = "sky/sky.cubemap.json"};
    const auto cooker = fixture.Cooker();
    const auto prepared = cooker.Prepare(request, error, warnings);
    INFO(error);
    REQUIRE(prepared);
    REQUIRE(prepared->Inputs.size() == 6);
    CHECK(prepared->Paths.SettingsPath == fixture.Source("sky/sky.cubemap.json"));
    CHECK(prepared->Paths.CookedPath == fixture.Cooked() / "sky/sky.cubemap.ktx2");
    CHECK(prepared->Paths.RecordPath == fixture.Cooked() / "sky/sky.cubemap.cook.json");
    CHECK(prepared->Settings.ColorSpace == TextureColorSpace::Linear);
    const std::array names{"right.png",  "left.png", "top.png",
                           "bottom.png", "back.png", "front.png"};
    for (size_t i = 0; i < names.size(); ++i)
        CHECK(prepared->Inputs[i] == fixture.Source(std::string("sky/") + names[i]));
    request.Overrides.Usage = TextureUsage::Normal;
    request.Overrides.bFlipNormalY = true;
    const auto overridden = cooker.Prepare(request, error, warnings);
    REQUIRE(overridden);
    REQUIRE(cooker.SaveSettings(*overridden, error));
    const auto saved = ParseCubemapDescriptor(
        TextureCookFixture::ReadText(overridden->Paths.SettingsPath), "sky.cubemap.json", error);
    REQUIRE(saved);
    CHECK(saved->Settings.bFlipNormalY);
    CHECK(saved->Settings.Usage == TextureUsage::Normal);
    CHECK(saved->Faces == overridden->Descriptor->Faces);
    CHECK(TextureCookFixture::ReadText(fixture.Source("sky/right.png.json")) ==
          R"({"version":99})");
    CHECK_FALSE(fs::exists(fixture.Source("sky/sky.cubemap.json.json")));
}

TEST_CASE("Cubemap face failures leave the descriptor unchanged", "[asset][texture-cooker]")
{
    TextureCookFixture fixture;
    fixture.WriteCubemap();
    const auto descriptor = fixture.Source("sky/sky.cubemap.json");
    const auto original = TextureCookFixture::ReadText(descriptor);
    SECTION("missing")
    {
        fs::remove(fixture.Source("sky/top.png"));
    }
    SECTION("nonsquare")
    {
        fixture.WriteImage("sky/top.png", 4, 2);
    }
    SECTION("different square size")
    {
        fixture.WriteImage("sky/top.png", 2, 2);
    }
    SECTION("corrupt")
    {
        TextureCookFixture::WriteText(fixture.Source("sky/top.png"), "not an image");
    }
    SECTION("outside root")
    {
        fixture.WriteImage("../outside.png");
        TextureCookFixture::WriteText(descriptor,
                                      Replace(original, "top.png", "../../outside.png"));
    }
    const auto before = TextureCookFixture::ReadText(descriptor);
    const auto result = fixture.Cooker().Cook({.Source = "sky/sky.cubemap.json"});
    CHECK(result.Status == TextureCookStatus::Failed);
    CHECK(Contains(result.Error, "faces.positiveY"));
    CHECK(TextureCookFixture::ReadText(descriptor) == before);
    CHECK_FALSE(fs::exists(fixture.Cooked()));
}

TEST_CASE("Cooker failures preserve files and never claim preparation is a cooked output",
          "[asset][texture-cooker]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("image.png");
    SECTION("blocked cooked root")
    {
        const auto blocked = fixture.Root() / "blocked";
        TextureCookFixture::WriteText(blocked, "keep");
        const auto result = TextureCooker(fixture.Content(), blocked).Cook({.Source = "image.png"});
        CHECK(result.Status == TextureCookStatus::Failed);
        CHECK(Contains(result.Error, "cooked-root"));
        CHECK_FALSE(fs::exists(fixture.Source("image.png.json")));
        CHECK(TextureCookFixture::ReadText(blocked) == "keep");
    }
    SECTION("encoder unavailable")
    {
        const auto result = fixture.Cooker().Cook({.Source = "image.png"});
        CHECK(result.Status == TextureCookStatus::Failed);
        CHECK(Contains(result.Error, "Texture encoding is not available"));
        CHECK(fs::exists(fixture.Source("image.png.json")));
        CHECK_FALSE(fs::exists(fixture.Cooked() / "image.png.ktx2"));
        CHECK_FALSE(fs::exists(fixture.Cooked() / "image.png.cook.json"));
        for (const auto& entry : fs::directory_iterator(fixture.Content()))
            CHECK_FALSE(Contains(entry.path().filename().string(), ".hikari-cook-"));
    }
}

TEST_CASE("Code requests enforce thread limits and inspect JPEG metadata",
          "[asset][texture-cooker]")
{
    TextureCookFixture fixture;
    fs::copy_file(HIKARI_TEXTURE_TEST_DATA_DIR "constant.jpg", fixture.Source("image.jpg"));
    std::string error;
    std::vector<std::string> warnings;
    TextureCookRequest request{.Source = "image.jpg"};
    const auto cooker = fixture.Cooker();
    for (uint32_t threads : {0u, 129u})
    {
        request.EncoderThreads = threads;
        CHECK_FALSE(cooker.Prepare(request, error, warnings));
        CHECK(Contains(error, "[1, 128]"));
    }
    for (uint32_t threads : {1u, 128u})
    {
        request.EncoderThreads = threads;
        const auto prepared = cooker.Prepare(request, error, warnings);
        REQUIRE(prepared);
        CHECK(prepared->ImageMetadata.front().Channels == 3);
        CHECK(prepared->ImageMetadata.front().Extent.Width == 4);
    }
}

TEST_CASE("Cooker roots remain stable when the working directory changes",
          "[asset][texture-cooker]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("image.png");
    struct RestoreDirectory
    {
        fs::path Original;
        ~RestoreDirectory()
        {
            std::error_code ec;
            fs::current_path(Original, ec);
        }
    } restore{fs::current_path()};
    fs::current_path(fixture.Root());
    const TextureCooker cooker("content", "custom cooked");
    fs::create_directory(fixture.Root() / "another directory");
    fs::current_path(fixture.Root() / "another directory");
    std::string error;
    std::vector<std::string> warnings;
    const auto prepared = cooker.Prepare({.Source = "image.png"}, error, warnings);
    REQUIRE(prepared);
    CHECK(prepared->Paths.SourcePath == fixture.Source("image.png"));
    CHECK(prepared->Paths.CookedPath == fixture.Root() / "custom cooked/image.png.ktx2");
}

#if !defined(_WIN32)
TEST_CASE("Read-only cooked directories fail before creating source settings",
          "[asset][texture-cooker]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("image.png");
    fs::create_directory(fixture.Cooked());
    fs::permissions(fixture.Cooked(), fs::perms::owner_read | fs::perms::owner_exec);
    const auto result = fixture.Cooker().Cook({.Source = "image.png"});
    fs::permissions(fixture.Cooked(), fs::perms::owner_all);
    if (Contains(result.Error, "Texture encoding is not available"))
        SKIP("Current user bypasses filesystem permission checks");
    CHECK(result.Status == TextureCookStatus::Failed);
    CHECK(Contains(result.Error, "--cooked-root"));
    CHECK_FALSE(fs::exists(fixture.Source("image.png.json")));
}
#endif
