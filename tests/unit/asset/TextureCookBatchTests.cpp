#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <ktx.h>
#include <vulkan/vulkan_core.h>

#include <asset/TextureCooker.h>

#include "DdsFixture.h"
#include "TextureCookFixture.h"
#include "TextureCookKtxFixture.h"

using namespace Hikari::Asset;
namespace fs = std::filesystem;

namespace
{
const TextureCookBatchEntry& Entry(const TextureCookBatchResult& batch, std::string_view source)
{
    const auto found =
        std::find_if(batch.Entries.begin(), batch.Entries.end(),
                     [&](const auto& item) { return item.Source.generic_string() == source; });
    REQUIRE(found != batch.Entries.end());
    INFO(found->Result.Error);
    return *found;
}

void Destroy(ktxTexture2* texture)
{
    ktxTexture_Destroy(ktxTexture(texture));
}
using KtxOwner = std::unique_ptr<ktxTexture2, decltype(&Destroy)>;
KtxOwner Open(const fs::path& path)
{
    ktxTexture2* texture = nullptr;
    const auto status = ktxTexture2_CreateFromNamedFile(
        path.string().c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture);
    INFO(ktxErrorString(status));
    REQUIRE(status == KTX_SUCCESS);
    return {texture, &Destroy};
}
} // namespace

TEST_CASE("Batch cooking mixes source types, retains saved semantics and skips unchanged outputs",
          "[asset][texture-batch]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("textures/color.PNG");
    fixture.WriteImage("textures/normal.png");
    fs::copy_file(HIKARI_TEXTURE_TEST_DATA_DIR "channels-gray.png",
                  fixture.Source("textures/scalar.png"));
    fs::copy_file(HIKARI_TEXTURE_TEST_DATA_DIR "constant.jpg",
                  fixture.Source("textures/photo.jpeg"));
    TextureCookFixture::WriteText(fixture.Source("native.dds"),
                                  MakeDdsFixture("DXT1", 8, 4, 4, 3).Bytes);
    fixture.WriteCubemap();
    const std::string normalSettings =
        R"({"version":1,"usage":"normal","flipNormalY":true,"mipmaps":false})";
    const std::string scalarSettings = R"({"version":1,"usage":"scalar"})";
    TextureCookFixture::WriteText(fixture.Source("textures/normal.png.texture.json"),
                                  normalSettings);
    TextureCookFixture::WriteText(fixture.Source("textures/scalar.png.texture.json"),
                                  scalarSettings);
    const auto cooker = fixture.Cooker();
    const auto first = cooker.CookAll({.EncoderThreads = 2});
    REQUIRE(first.Succeeded());
    REQUIRE(first.Entries.size() == 6);
    for (const auto& item : first.Entries)
    {
        INFO(item.Result.Error);
        CHECK(item.Result.Status == TextureCookStatus::Cooked);
    }
    CHECK(first.Entries.front().Source.generic_string() == "sky/sky.cubemap.json");
    CHECK(Open(Entry(first, "textures/normal.png").Result.Output)->vkFormat ==
          VK_FORMAT_BC5_UNORM_BLOCK);
    CHECK(Open(Entry(first, "textures/scalar.png").Result.Output)->vkFormat ==
          VK_FORMAT_BC4_UNORM_BLOCK);
    CHECK(Open(Entry(first, "textures/photo.jpeg").Result.Output)->vkFormat ==
          VK_FORMAT_BC7_SRGB_BLOCK);
    const auto cube = Open(Entry(first, "sky/sky.cubemap.json").Result.Output);
    CHECK(cube->numFaces == 6);
    CHECK(cube->numLevels == 3);
    CHECK_FALSE(fs::exists(fixture.Cooked() / "sky/right.png.ktx2"));
    CHECK_FALSE(fs::exists(fixture.Source("sky/right.png.texture.json")));
    CHECK(TextureCookFixture::ReadText(fixture.Source("textures/normal.png.texture.json")) ==
          normalSettings);
    CHECK(TextureCookFixture::ReadText(fixture.Source("textures/scalar.png.texture.json")) ==
          scalarSettings);
    CHECK(fs::exists(fixture.Source("textures/color.PNG.texture.json")));
    const auto warm = cooker.CookAll({.EncoderThreads = 2});
    REQUIRE(warm.Succeeded());
    for (const auto& item : warm.Entries)
        CHECK(item.Result.Status == TextureCookStatus::Skipped);
    fixture.WriteImage("textures/color.PNG", 8, 4);
    const auto edited = cooker.CookAll({.EncoderThreads = 2});
    REQUIRE(edited.Succeeded());
    for (const auto& item : edited.Entries)
        CHECK(item.Result.Status == (item.Source == fs::path("textures/color.PNG")
                                         ? TextureCookStatus::Cooked
                                         : TextureCookStatus::Skipped));
    const auto forced = cooker.CookAll({.bForce = true, .EncoderThreads = 2});
    REQUIRE(forced.Succeeded());
    for (const auto& item : forced.Entries)
        CHECK(item.Result.Status == TextureCookStatus::Cooked);
    CHECK(TextureCookFixture::ReadText(fixture.Source("textures/normal.png.texture.json")) ==
          normalSettings);
}

TEST_CASE("Batch failures retain earlier assets and continue serially with operational options",
          "[asset][texture-batch]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("a.png");
    fixture.WriteImage("b.png");
    auto encoder = TextureCookFixtureEncoder();
    const auto cooker = fixture.Cooker();
    REQUIRE(cooker.CookAll({}, encoder).Succeeded());
    const auto original = TextureCookFixture::ReadText(fixture.Cooked() / "a.png.ktx2");
    const auto record = TextureCookFixture::ReadText(fixture.Cooked() / "a.png.cook.json");
    bool bEncoding = false;
    std::vector<fs::path> order;
    encoder.Encode = [&](const auto& prepared, const auto& output, auto& error)
    {
        CHECK_FALSE(bEncoding);
        bEncoding = true;
        order.push_back(prepared.Paths.SourceRelative);
        CHECK(prepared.Request.EncoderThreads == 128);
        CHECK(prepared.Request.EncodingQuality == 0.5);
        CHECK(prepared.Request.bForce);
        CHECK(prepared.Request.Overrides.IsEmpty());
        bool bSuccess = false;
        if (prepared.Paths.SourceRelative == "a.png")
        {
            TextureCookFixture::WriteText(output, "partial output");
            error = "intentional batch failure";
        }
        else
            bSuccess = WriteTextureCookKtxFixture(prepared, output, error);
        bEncoding = false;
        return bSuccess;
    };
    const auto result =
        cooker.CookAll({.bForce = true, .EncoderThreads = 128, .EncodingQuality = 0.5}, encoder);
    CHECK_FALSE(result.Succeeded());
    CHECK(result.Errors.empty());
    REQUIRE(result.Entries.size() == 2);
    CHECK(Entry(result, "a.png").Result.Status == TextureCookStatus::Failed);
    CHECK(Entry(result, "b.png").Result.Status == TextureCookStatus::Cooked);
    CHECK(order == std::vector<fs::path>{"a.png", "b.png"});
    CHECK(TextureCookFixture::ReadText(fixture.Cooked() / "a.png.ktx2") == original);
    CHECK(TextureCookFixture::ReadText(fixture.Cooked() / "a.png.cook.json") == record);
    CHECK_FALSE(fs::exists(fixture.Cooked() / "a.png.ktx2.transaction"));
}

TEST_CASE("Failed descriptors reserve recoverable references and leave unrelated images cookable",
          "[asset][texture-batch]")
{
    TextureCookFixture fixture;
    fixture.WriteCubemap();
    fixture.WriteImage("other.png");
    auto text = TextureCookFixture::ReadText(fixture.Source("sky/sky.cubemap.json"));
    SECTION("invalid settings version")
    {
        text.replace(text.find("\"version\":1"), 11, "\"version\":99");
    }
    SECTION("truncated JSON after all face references")
    {
        text.pop_back();
    }
    SECTION("duplicate face keys preserve both referenced paths")
    {
        fixture.WriteImage("sky/alternate.png");
        const auto start = text.find("\"positiveX\":\"right.png\"");
        REQUIRE(start != std::string::npos);
        text.replace(start, 23, "\"positiveX\":\"right.png\",\"positiveX\":\"alternate.png\"");
    }
    SECTION("unknown face key still reserves its path")
    {
        fixture.WriteImage("sky/alternate.png");
        text.insert(text.find("\"faces\":{") + 9, "\"notAFace\":\"alternate.png\",");
    }
    SECTION("missing face")
    {
        fs::remove(fixture.Source("sky/top.png"));
    }
    TextureCookFixture::WriteText(fixture.Source("sky/sky.cubemap.json"), text);
    const auto result = fixture.Cooker().CookAll({}, TextureCookFixtureEncoder());
    CHECK_FALSE(result.Succeeded());
    REQUIRE(result.Entries.size() == 2);
    CHECK(Entry(result, "sky/sky.cubemap.json").Result.Status == TextureCookStatus::Failed);
    CHECK(Entry(result, "other.png").Result.Status == TextureCookStatus::Cooked);
    CHECK_FALSE(fs::exists(fixture.Cooked() / "sky/right.png.ktx2"));
    CHECK_FALSE(fs::exists(fixture.Source("sky/right.png.texture.json")));
    CHECK_FALSE(fs::exists(fixture.Cooked() / "sky/alternate.png.ktx2"));
    CHECK(TextureCookFixture::ReadText(fixture.Source("sky/sky.cubemap.json")) == text);
    const auto single =
        fixture.Cooker().Cook({.Source = "sky/right.png"}, TextureCookFixtureEncoder());
    CHECK(single.Status == TextureCookStatus::Cooked);
}

TEST_CASE("Encoder failures keep cubemap face reservations while other assets succeed",
          "[asset][texture-batch]")
{
    TextureCookFixture fixture;
    fixture.WriteCubemap();
    fixture.WriteImage("other.png");
    auto encoder = TextureCookFixtureEncoder();
    encoder.Encode = [](const auto& prepared, const auto& output, auto& error)
    {
        if (prepared.Descriptor)
        {
            error = "intentional cubemap encoder failure";
            return false;
        }
        return WriteTextureCookKtxFixture(prepared, output, error);
    };
    const auto result = fixture.Cooker().CookAll({}, encoder);
    CHECK_FALSE(result.Succeeded());
    REQUIRE(result.Entries.size() == 2);
    CHECK(Entry(result, "sky/sky.cubemap.json").Result.Status == TextureCookStatus::Failed);
    CHECK(Entry(result, "other.png").Result.Status == TextureCookStatus::Cooked);
    CHECK_FALSE(fs::exists(fixture.Cooked() / "sky/right.png.ktx2"));
    CHECK_FALSE(fs::exists(fixture.Source("sky/right.png.texture.json")));
}

TEST_CASE("Batch discovery excludes nested generated trees and directory symlinks",
          "[asset][texture-batch]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("input.png");
    fixture.WriteImage("generated/trap.png");
    TextureCookFixture::WriteText(fixture.Source("generated/broken.dds"), "not DDS");
    TextureCookFixture::WriteText(fixture.Source("ignore.json"), "not a descriptor");
    const auto generated = fixture.Source("generated");
    const TextureCooker cooker(fixture.Content(), generated);
    SECTION("generated subtree") {}
    SECTION("directory and file aliases")
    {
        fs::create_directory(fixture.Root() / "foreign");
        TextureCookFixture::WriteText(fixture.Root() / "foreign/hidden.png", "not PNG");
        std::error_code ec;
        fs::create_directory_symlink(fixture.Root() / "foreign", fixture.Source("outside"), ec);
        if (ec)
            SKIP("Symlink creation unavailable: " + ec.message());
        fs::create_directory_symlink(generated, fixture.Source("generated alias"), ec);
        REQUIRE_FALSE(ec);
        fs::create_symlink(generated / "trap.png", fixture.Source("alias.png"), ec);
        REQUIRE_FALSE(ec);
    }
    const auto first = cooker.CookAll({}, TextureCookFixtureEncoder());
    REQUIRE(first.Succeeded());
    REQUIRE(first.Entries.size() == 1);
    CHECK(first.Entries[0].Source == "input.png");
    CHECK_FALSE(fs::exists(fixture.Source("generated/trap.png.texture.json")));
    const auto warm = cooker.CookAll({}, TextureCookFixtureEncoder());
    REQUIRE(warm.Succeeded());
    REQUIRE(warm.Entries.size() == 1);
    CHECK(warm.Entries[0].Result.Status == TextureCookStatus::Skipped);
}

TEST_CASE("Batch discovery reports invalid sources without stopping successful items",
          "[asset][texture-batch]")
{
    TextureCookFixture fixture;
    TextureCookFixture::WriteText(fixture.Source("a-broken.png"), "not PNG");
    fixture.WriteImage("z-valid.png");
    const auto result = fixture.Cooker().CookAll({}, TextureCookFixtureEncoder());
    CHECK_FALSE(result.Succeeded());
    REQUIRE(result.Entries.size() == 2);
    CHECK(result.Entries[0].Source == "a-broken.png");
    CHECK(result.Entries[0].Result.Status == TextureCookStatus::Failed);
    CHECK(result.Entries[1].Result.Status == TextureCookStatus::Cooked);
    CHECK_FALSE(fs::exists(fixture.Source("a-broken.png.texture.json")));
    CHECK(fs::exists(fixture.Cooked() / "z-valid.png.ktx2"));
}

TEST_CASE("Batch configuration errors and empty trees leave the filesystem unchanged",
          "[asset][texture-batch]")
{
    TextureCookFixture fixture;
    const auto empty = fixture.Cooker().CookAll();
    CHECK(empty.Succeeded());
    CHECK(empty.Entries.empty());
    CHECK_FALSE(fs::exists(fixture.Cooked()));
    fixture.WriteImage("image.png");
    for (const uint32_t threads : {129u, 256u})
    {
        const auto bad = fixture.Cooker().CookAll({.EncoderThreads = threads});
        CHECK_FALSE(bad.Succeeded());
        CHECK(bad.Entries.empty());
        REQUIRE(bad.Errors.size() == 1);
        CHECK(bad.Errors[0].find("threads") != std::string::npos);
    }
    for (const double quality : {-0.1, 1.1, std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN()})
    {
        const auto bad = fixture.Cooker().CookAll({.EncodingQuality = quality});
        CHECK_FALSE(bad.Succeeded());
        CHECK(bad.Entries.empty());
        REQUIRE(bad.Errors.size() == 1);
        CHECK(bad.Errors[0].find("quality") != std::string::npos);
    }
    const auto missing = TextureCooker(fixture.Root() / "missing").CookAll();
    CHECK_FALSE(missing.Succeeded());
    CHECK_FALSE(missing.Errors.empty());
    const auto overlap = TextureCooker(fixture.Content(), fixture.Root()).CookAll();
    CHECK_FALSE(overlap.Succeeded());
    CHECK_FALSE(overlap.Errors.empty());
    const auto blocked = fixture.Root() / "blocked";
    TextureCookFixture::WriteText(blocked, "keep");
    CHECK_FALSE(TextureCooker(fixture.Content(), blocked).CookAll().Succeeded());
    CHECK(TextureCookFixture::ReadText(blocked) == "keep");
    CHECK_FALSE(fs::exists(fixture.Source("image.png.texture.json")));
    CHECK_FALSE(fs::exists(fixture.Cooked()));
}
