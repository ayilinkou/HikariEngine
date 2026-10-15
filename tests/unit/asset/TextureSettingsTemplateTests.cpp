#include <catch2/catch_test_macros.hpp>

#include <array>
#include <barrier>
#include <filesystem>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include <asset/CubemapDescriptor.h>
#include <asset/TextureCooker.h>
#include <nlohmann/json.hpp>

#include "CookFileUtil.h"
#include "TextureCookFixture.h"
#include "TextureCookKtxFixture.h"

using namespace Hikari::Asset;
namespace fs = std::filesystem;

TEST_CASE("Settings templates use shared defaults without images or a content root",
          "[asset][texture-template]")
{
    TextureCookFixture fixture;
    fs::remove(fixture.Content());
    std::string error;
    for (const auto name : {"mytex", "brick.png", "brick.jpg", "brick.dds"})
    {
        const auto base = fixture.Root() / name;
        const auto created = TextureCooker::CreateTextureSettings(base, {}, false, error);
        INFO(error);
        REQUIRE(created);
        CHECK(*created == fs::path(base.string() + ".texture.json"));
        CHECK_FALSE(fs::exists(base));
        const auto settings =
            ParseTextureSettings(TextureCookFixture::ReadText(*created), created->string(), error);
        REQUIRE(settings);
        CHECK(*settings == TextureSettings{});
    }
}

TEST_CASE("Cubemap templates have empty faces until authors configure their images",
          "[asset][texture-template]")
{
    TextureCookFixture fixture;
    fs::create_directory(fixture.Source("sky"));
    std::string error;
    const auto created =
        TextureCooker::CreateCubemapSettings(fixture.Source("sky/sky"), {}, false, error);
    INFO(error);
    REQUIRE(created);
    CHECK(*created == fixture.Source("sky/sky.cubemap.json"));
    const auto text = TextureCookFixture::ReadText(*created);
    auto document = nlohmann::json::parse(text);
    CHECK(document["faces"].size() == 6);
    for (const auto face : kCubemapFaceNames)
        CHECK(document["faces"][std::string(face)] == "");
    document.erase("faces");
    CHECK(ParseTextureSettings(document.dump(), created->string(), error) == TextureSettings{});
    CHECK_FALSE(ParseCubemapDescriptor(text, created->string(), error));
    CHECK(error.find("faces.positiveX") != std::string::npos);
    std::vector<std::string> warnings;
    CHECK_FALSE(fixture.Cooker().Prepare({.Source = "sky/sky.cubemap.json"}, error, warnings));
    CHECK(TextureCookFixture::ReadText(*created) == text);
    fixture.WriteCubemap();
    CHECK(fixture.Cooker().Prepare({.Source = "sky/sky.cubemap.json"}, error, warnings));
}

TEST_CASE("Template overrides preserve auto defaults and validate without source inspection",
          "[asset][texture-template]")
{
    TextureCookFixture fixture;
    std::string error;
    TextureSettingsOverrides overrides;
    overrides.Usage = TextureUsage::Normal;
    overrides.bFlipNormalY = true;
    overrides.bNoMips = true;
    const auto path =
        TextureCooker::CreateTextureSettings(fixture.Source("normal"), overrides, false, error);
    INFO(error);
    REQUIRE(path);
    const auto settings =
        ParseTextureSettings(TextureCookFixture::ReadText(*path), path->string(), error);
    REQUIRE(settings);
    CHECK(settings->Usage == TextureUsage::Normal);
    CHECK(settings->Format == TextureCompression::Auto);
    CHECK(settings->ColorSpace == TextureColorSpace::Auto);
    CHECK(settings->bFlipNormalY);
    CHECK_FALSE(settings->bMipmaps);
    const auto original = TextureCookFixture::ReadText(*path);
    for (const auto cutoff : {-0.1, 1.1, std::numeric_limits<double>::quiet_NaN()})
    {
        overrides.AlphaCutoff = cutoff;
        CHECK_FALSE(
            TextureCooker::CreateTextureSettings(fixture.Source("normal"), overrides, true, error));
        CHECK(TextureCookFixture::ReadText(*path) == original);
    }
    overrides.AlphaCutoff = 0.5;
    overrides.ColorSpace = TextureColorSpace::Srgb;
    CHECK_FALSE(
        TextureCooker::CreateTextureSettings(fixture.Source("normal"), overrides, true, error));
    CHECK(TextureCookFixture::ReadText(*path) == original);
    overrides = {};
    overrides.Format = TextureCompression::BC5Signed;
    overrides.ColorSpace = TextureColorSpace::Linear;
    CHECK(TextureCooker::CreateTextureSettings(fixture.Source("future-dds"), overrides, false,
                                               error));
}

TEST_CASE("Template creation requires an existing parent and explicit replacement",
          "[asset][texture-template]")
{
    TextureCookFixture fixture;
    std::string error;
    for (const fs::path& name :
         {fs::path{}, fs::path("."), fs::path(".."), fixture.Source("missing/new-texture")})
        CHECK_FALSE(TextureCooker::CreateTextureSettings(name, {}, false, error));
    CHECK_FALSE(fs::exists(fixture.Source("missing")));
    const auto name = fixture.Source("existing");
    const auto path = fixture.Source("existing.texture.json");
    TextureCookFixture::WriteText(path, "authored settings");
    CHECK_FALSE(TextureCooker::CreateTextureSettings(name, {}, false, error));
    CHECK(TextureCookFixture::ReadText(path) == "authored settings");
    CHECK(TextureCooker::CreateTextureSettings(name, {}, true, error));
    CHECK(ParseTextureSettings(TextureCookFixture::ReadText(path), path.string(), error));
    fs::create_directory(fixture.Source("directory.texture.json"));
    CHECK_FALSE(TextureCooker::CreateTextureSettings(fixture.Source("directory"), {}, true, error));
    CHECK(fs::is_directory(fixture.Source("directory.texture.json")));
    const auto link = fixture.Source("link.texture.json");
    std::error_code ec;
    fs::create_symlink(path, link, ec);
    if (!ec)
    {
        const auto original = TextureCookFixture::ReadText(path);
        CHECK_FALSE(TextureCooker::CreateTextureSettings(fixture.Source("link"), {}, true, error));
        CHECK(fs::is_symlink(link));
        CHECK(TextureCookFixture::ReadText(path) == original);
    }
}

TEST_CASE("Concurrent template publication never overwrites another writer",
          "[asset][texture-template]")
{
    TextureCookFixture fixture;
    const auto path = fixture.Source("shared.texture.json");
    std::barrier start(4);
    std::array<bool, 4> results{};
    std::array<std::string, 4> errors;
    std::vector<std::thread> threads;
    for (size_t i = 0; i < results.size(); ++i)
        threads.emplace_back(
            [&, i]
            {
                start.arrive_and_wait();
                results[i] = WriteCookText(path, std::to_string(i), errors[i], false);
            });
    for (auto& thread : threads)
        thread.join();
    size_t winners = 0;
    for (size_t i = 0; i < results.size(); ++i)
        if (results[i])
        {
            ++winners;
            CHECK(TextureCookFixture::ReadText(path) == std::to_string(i));
        }
        else
            CHECK_FALSE(errors[i].empty());
    CHECK(winners == 1);
    size_t files = 0;
    for (const auto& entry : fs::directory_iterator(fixture.Content()))
    {
        CHECK(entry.path() == path);
        ++files;
    }
    CHECK(files == 1);
}

TEST_CASE("Cooking ignores legacy sidecars and reads only the new settings name",
          "[asset][texture-template][texture-cooker]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("image.png");
    const auto legacy = fixture.Source("image.png.json");
    const std::string legacyText = R"({"version":1,"usage":"normal"})";
    TextureCookFixture::WriteText(legacy, legacyText);
    const auto result = fixture.Cooker().Cook({.Source = "image.png"}, TextureCookFixtureEncoder());
    INFO(result.Error);
    REQUIRE(result.Status == TextureCookStatus::Cooked);
    CHECK(result.Output == fixture.Cooked() / "image.png.ktx2");
    CHECK(TextureCookFixture::ReadText(legacy) == legacyText);
    std::string error;
    const auto settings =
        ParseTextureSettings(TextureCookFixture::ReadText(fixture.Source("image.png.texture.json")),
                             "new settings", error);
    REQUIRE(settings);
    CHECK(settings->Usage == TextureUsage::Color);
    TextureCookFixture::WriteText(fixture.Source("image.png.texture.json"), legacyText);
    std::vector<std::string> warnings;
    const auto prepared = fixture.Cooker().Prepare({.Source = "image.png"}, error, warnings);
    REQUIRE(prepared);
    CHECK(prepared->Settings.Authored.Usage == TextureUsage::Normal);
}
