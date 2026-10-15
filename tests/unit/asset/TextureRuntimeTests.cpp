#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <barrier>
#include <future>
#include <string>
#include <vector>

#include <asset/KtxReader.h>
#include <asset/TextureCooker.h>

#include "CookRecord.h"
#include "DdsFixture.h"
#include "PixelDdsFixture.h"
#include "TextureCookFixture.h"
#include "TextureCookKtxFixture.h"

using namespace Hikari::Asset;

TEST_CASE("Runtime BC1 selection respects authored usage format and alpha",
          "[asset][texture-cooker][runtime]")
{
    TextureCookFixture files;
    files.WriteImage("image.png");
    struct Case
    {
        std::string Settings;
        TextureCompression Format;
    };
    for (const auto& entry :
         {Case{R"({"version":1})", TextureCompression::BC1},
          Case{R"({"version":1,"format":"auto","alphaMode":"opaque"})", TextureCompression::BC1},
          Case{R"({"version":1,"usage":"color","colorSpace":"linear"})", TextureCompression::BC1},
          Case{R"({"version":1,"format":"bc7"})", TextureCompression::BC7},
          Case{R"({"version":1,"usage":"normal"})", TextureCompression::BC5},
          Case{R"({"version":1,"usage":"scalar"})", TextureCompression::BC4},
          Case{R"({"version":1,"usage":"packed"})", TextureCompression::BC7},
          Case{R"({"version":1,"alphaMode":"mask","mipmaps":false})", TextureCompression::BC7}})
    {
        INFO(entry.Settings);
        const auto settingsPath = files.Source("image.png.texture.json");
        TextureCookFixture::WriteText(settingsPath, entry.Settings);
        std::string error;
        std::vector<std::string> warnings;
        const auto prepared =
            files.Cooker().Prepare({.Source = "image.png", .bRuntime = true}, error, warnings);
        INFO(error);
        REQUIRE(prepared);
        CHECK(prepared->Settings.Format == entry.Format);
        REQUIRE(files.Cooker().SaveSettings(*prepared, error));
        CHECK(TextureCookFixture::ReadText(settingsPath) == entry.Settings);
    }
}

TEST_CASE("Runtime format selection preserves native DDS and requires known opaque pixel alpha",
          "[asset][texture-cooker][runtime]")
{
    TextureCookFixture files;
    std::string error;
    std::vector<std::string> warnings;
    const auto prepare = [&]
    { return files.Cooker().Prepare({.Source = "image.dds", .bRuntime = true}, error, warnings); };
    for (const auto dxgi : {71u, 77u, 98u})
    {
        const auto dds = MakeDdsFixture("DX10", dxgi == 71 ? 8 : 16, 4, 4, 1, 1, dxgi, 3);
        TextureCookFixture::WriteText(files.Source("image.dds"), dds.Bytes);
        const auto prepared = prepare();
        INFO(error);
        REQUIRE(prepared);
        CHECK(prepared->Settings.Format == prepared->Dds->Format);
        CHECK_FALSE(prepared->Settings.bRecompressExistingMips);
    }
    const auto pixels = MakePixelDdsFixture(DdsPixelLayout::RGBA8, 4, 4, 1, 1, false, 0,
                                            [](uint32_t, uint32_t, uint32_t, uint32_t)
                                            { return std::array<uint8_t, 4>{0, 0, 0, 90}; });
    TextureCookFixture::WriteText(files.Source("image.dds"), pixels.Bytes);
    const auto unknown = prepare();
    INFO(error);
    REQUIRE(unknown);
    CHECK_FALSE(unknown->Settings.bAlphaUsageKnown);
    CHECK(unknown->Settings.Format == TextureCompression::BC7);
    TextureCookFixture::WriteText(files.Source("image.dds.texture.json"),
                                  R"({"version":1,"alphaMode":"opaque"})");
    const auto opaque = prepare();
    INFO(error);
    REQUIRE(opaque);
    CHECK(opaque->Settings.Format == TextureCompression::BC1);
}

TEST_CASE("Runtime BC1 records upgrade offline and reuse compatible BC7 at the requested quality",
          "[asset][texture-cooker][incremental][runtime][quality]")
{
    TextureCookFixture files;
    files.WriteImage("image.png");
    const auto settingsPath = files.Source("image.png.texture.json");
    const std::string settings = " {\"version\":1,\"format\":\"auto\"}\n";
    TextureCookFixture::WriteText(settingsPath, settings);
    const auto recordPath = files.Cooked() / "image.png.cook.json";
    auto encoder = TextureCookFixtureEncoder();
    int calls = 0;
    encoder.Encode = [&](const auto& prepared, const auto& output, auto& error)
    {
        ++calls;
        return WriteTextureCookKtxFixture(prepared, output, error);
    };
    TextureCookRequest request{.Source = "image.png", .EncodingQuality = 0.5, .bRuntime = true};
    const auto cook = [&](TextureCookStatus status, Hikari::Rhi::Format format, int expectedCalls)
    {
        const auto result = files.Cooker().Cook(request, encoder);
        INFO(result.Error);
        REQUIRE(result.Status == status);
        CHECK(calls == expectedCalls);
        std::string error;
        const auto texture = ReadKtxTexture(result.Output, error);
        INFO(error);
        REQUIRE(texture);
        CHECK(texture->Format == format);
        CHECK(TextureCookFixture::ReadText(settingsPath) == settings);
    };
    cook(TextureCookStatus::Cooked, Hikari::Rhi::Format::BC1Srgb, 1);
    const auto bc1 = ReadCookRecord(recordPath);
    REQUIRE(bc1);
    CHECK(bc1->EncodingQuality == 0.5);
    cook(TextureCookStatus::Skipped, Hikari::Rhi::Format::BC1Srgb, 1);
    request.bRuntime = false;
    cook(TextureCookStatus::Cooked, Hikari::Rhi::Format::BC7Srgb, 2);
    const auto bc7 = ReadCookRecord(recordPath);
    REQUIRE(bc7);
    CHECK(bc7->Fingerprint != bc1->Fingerprint);
    const auto offlineRecord = TextureCookFixture::ReadText(recordPath);
    request.bRuntime = true;
    cook(TextureCookStatus::Skipped, Hikari::Rhi::Format::BC7Srgb, 2);
    CHECK(TextureCookFixture::ReadText(recordPath) == offlineRecord);
    // Low-quality BC7 does not satisfy a higher-quality runtime request.
    request.EncodingQuality = 1.0;
    cook(TextureCookStatus::Cooked, Hikari::Rhi::Format::BC1Srgb, 3);
    request.bRuntime = false;
    cook(TextureCookStatus::Cooked, Hikari::Rhi::Format::BC7Srgb, 4);
    request.bRuntime = true;
    request.EncodingQuality = 0.25;
    cook(TextureCookStatus::Skipped, Hikari::Rhi::Format::BC7Srgb, 4);
    request.bForce = true;
    cook(TextureCookStatus::Cooked, Hikari::Rhi::Format::BC1Srgb, 5);
    const auto forced = ReadCookRecord(recordPath);
    REQUIRE(forced);
    CHECK(forced->EncodingQuality == 0.25);
    CHECK(forced->Fingerprint == bc1->Fingerprint);
    request.bForce = false;
    request.bRuntime = false;
    cook(TextureCookStatus::Cooked, Hikari::Rhi::Format::BC7Srgb, 6);
    TextureCookFixture::WriteText(files.Cooked() / "image.png.ktx2", "damaged BC7");
    request.bRuntime = true;
    cook(TextureCookStatus::Cooked, Hikari::Rhi::Format::BC1Srgb, 7);
    // A saved format edit must prevent reuse of the previous runtime BC1 recipe.
    TextureCookFixture::WriteText(settingsPath, R"({"version":1,"format":"bc7"})");
    const auto edited = files.Cooker().Cook(request, encoder);
    INFO(edited.Error);
    REQUIRE(edited.Status == TextureCookStatus::Cooked);
    CHECK(calls == 8);
}

TEST_CASE("Concurrent runtime and offline requests finish with reusable offline BC7",
          "[asset][texture-cooker][incremental][runtime]")
{
    TextureCookFixture files;
    files.WriteImage("image.png");
    std::barrier start(2);
    std::atomic_int calls = 0;
    auto encoder = TextureCookFixtureEncoder();
    encoder.Encode = [&](const auto& prepared, const auto& output, auto& error)
    {
        ++calls;
        return WriteTextureCookKtxFixture(prepared, output, error);
    };
    const auto cook = [&](bool runtime)
    {
        start.arrive_and_wait();
        return files.Cooker().Cook({.Source = "image.png", .bRuntime = runtime}, encoder);
    };
    auto runtime = std::async(std::launch::async, cook, true);
    auto offline = std::async(std::launch::async, cook, false);
    const auto a = runtime.get(), b = offline.get();
    INFO(a.Error);
    INFO(b.Error);
    REQUIRE(a.Status != TextureCookStatus::Failed);
    REQUIRE(b.Status == TextureCookStatus::Cooked);
    CHECK(calls >= 1);
    CHECK(calls <= 2);
    std::string error;
    const auto texture = ReadKtxTexture(b.Output, error);
    INFO(error);
    REQUIRE(texture);
    CHECK(texture->Format == Hikari::Rhi::Format::BC7Srgb);
    CHECK(files.Cooker().Cook({.Source = "image.png", .bRuntime = true}, encoder).Status ==
          TextureCookStatus::Skipped);
}
