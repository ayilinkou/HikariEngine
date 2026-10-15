#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <memory>
#include <string>

#include <rhi/IDevice.h>
#include <rhi/UniqueHandle.h>

#include "AssetRegistry.h"
#include "CaptureStream.h"
#include "Cubemap.h"
#include "DdsFixture.h"
#include "KtxFixture.h"
#include "MaterialFactory.h"
#include "RhiTestFixture.h"
#include "TextureCookFixture.h"
#include "ValidationGuard.h"

using namespace Hikari;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("Registry acquisition caches source identity and warns without overriding saved usage",
          "[rhi][gpu][resources][acquisition]")
{
    auto& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);
    TextureCookFixture files;
    const auto dds = MakeDdsFixture("DX10", 16, 8, 8, 4, 1, 98);
    const auto path = files.Source("source.dds");
    TextureCookFixture::WriteText(path, dds.Bytes);
    TextureCookFixture::WriteText(path.string() + ".texture.json",
                                  R"({"version":1,"usage":"normal"})");
    Platform::Paths paths(files.Content().string());
    const auto context = device.CreateUploadContext({});
    const Rhi::UniqueHandle<Rhi::SamplerHandle> sampler(
        device, device.CreateSampler({.DebugName = "Acquisition sampler"}));
    MaterialFactory materials(device, sampler.Get());
    AssetRegistry assets(device, *context, paths, materials, files.Cooked(), HIKARI_RELEASE_COOKER);
    const auto texture = assets.LoadTexture("source.dds", Asset::TextureUsage::Normal);
    REQUIRE(texture);
    CHECK(device.GetTextureDesc(texture->GetHandle())->Format == Rhi::Format::BC7Unorm);
    CHECK(device.GetTextureDesc(texture->GetHandle())->MipLevels == 4);
    CHECK(texture->GetMetadata().NormalEncoding == Asset::TextureNormalEncoding::XyzUnorm);
    const auto uploads = context->GetStats().Uploads;
    REQUIRE(uploads == 1);
    std::shared_ptr<Texture> alias;
    const auto log = CaptureStream(
        stderr, [&] { alias = assets.LoadTexture(path.string(), Asset::TextureUsage::Color); });
    CHECK(alias == texture);
    CHECK(context->GetStats().Uploads == uploads);
    CHECK_THAT(log, ContainsSubstring("expects color"));
    CHECK_THAT(log, ContainsSubstring("saved usage is normal"));
    CHECK_THAT(log, ContainsSubstring("source.dds.texture.json"));
    CHECK_THAT(log, ContainsSubstring("recook"));
    // A live reference is reused without even reopening now-invalid settings.
    TextureCookFixture::WriteText(path.string() + ".texture.json", "invalid");
    CHECK(assets.LoadTexture("./source.dds", Asset::TextureUsage::Normal) == texture);
    CHECK(context->GetStats().Uploads == uploads);
}

TEST_CASE("Registry acquisition returns fallbacks for failed cooks and retries after repair",
          "[rhi][gpu][resources][acquisition]")
{
    auto& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);
    TextureCookFixture files;
    const auto dds = MakeDdsFixture("DX10", 8, 8, 8, 4, 1, 71);
    const auto path = files.Source("retry.dds");
    TextureCookFixture::WriteText(path, dds.Bytes);
    Platform::Paths paths(files.Content().string());
    const auto context = device.CreateUploadContext({});
    const Rhi::UniqueHandle<Rhi::SamplerHandle> sampler(
        device, device.CreateSampler({.DebugName = "Retry sampler"}));
    MaterialFactory materials(device, sampler.Get());
    AssetRegistry assets(device, *context, paths, materials, files.Cooked(), HIKARI_RELEASE_COOKER);
    auto first = assets.LoadTexture("retry.dds");
    REQUIRE(first);
    const auto output = files.Cooked() / "retry.dds.ktx2";
    const auto previous = TextureCookFixture::ReadText(output);
    first.reset();
    TextureCookFixture::WriteText(path.string() + ".texture.json", R"({"version":99})");
    auto fallback = assets.LoadTexture("retry.dds");
    REQUIRE(fallback);
    CHECK(device.GetTextureDesc(fallback->GetHandle())->Format == Rhi::Format::RGBA8Srgb);
    CHECK(device.GetTextureDesc(fallback->GetHandle())->Extent.Width == 1);
    CHECK(TextureCookFixture::ReadText(output) == previous);
    TextureCookFixture::WriteText(path.string() + ".texture.json",
                                  R"({"version":1,"usage":"packed"})");
    const auto recovered = assets.LoadTexture("retry.dds", Asset::TextureUsage::Packed);
    REQUIRE(recovered);
    CHECK(recovered != fallback);
    CHECK(recovered->GetMetadata().Usage == Asset::TextureUsage::Packed);
    CHECK(device.GetTextureDesc(recovered->GetHandle())->Format == Rhi::Format::BC1Unorm);
}

TEST_CASE("Registry loads cooked 2D and cubemap assets without a cooker and rejects wrong shapes",
          "[rhi][gpu][resources][acquisition]")
{
    auto& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);
    TextureCookFixture files;
    const auto cookedRoot = files.Root() / "custom cooked";
    const auto image = MakeKtxFixture(145, 8, 8, 4, 1, {{"HikariTextureUsage", "color"}});
    const auto cube = MakeKtxFixture(145, 8, 8, 4, 6, {{"HikariTextureUsage", "color"}});
    TextureCookFixture::WriteText(cookedRoot / "image.ktx2", image.Bytes);
    TextureCookFixture::WriteText(cookedRoot / "cube.ktx2", cube.Bytes);
    files.WriteImage("uncooked.png");
    Platform::Paths paths(files.Content().string());
    const auto context = device.CreateUploadContext({});
    const Rhi::UniqueHandle<Rhi::SamplerHandle> sampler(
        device, device.CreateSampler({.DebugName = "Direct sampler"}));
    MaterialFactory materials(device, sampler.Get());
    AssetRegistry assets(device, *context, paths, materials, cookedRoot,
                         files.Root() / "missing cooker");
    const auto texture = assets.LoadTexture("image.ktx2");
    REQUIRE(texture);
    CHECK(device.GetTextureDesc(texture->GetHandle())->Format == Rhi::Format::BC7Unorm);
    CHECK(assets.LoadTexture((cookedRoot / "image.ktx2").string()) == texture);
    const auto cubemap = assets.LoadCubemap("cube.ktx2", "Direct cube");
    REQUIRE(cubemap);
    CHECK(cubemap->GetName() == "Direct cube");
    CHECK(device.GetTextureDesc(cubemap->GetHandle())->ArrayLayers == 6);
    CHECK(device.GetTextureDesc(cubemap->GetHandle())->MipLevels == 4);
    CHECK(assets.LoadCubemap("./cube.ktx2") == cubemap);
    const auto badCube = assets.LoadCubemap("image.ktx2");
    REQUIRE(badCube);
    CHECK(badCube->GetName() == "FallbackCubemap");
    CHECK(device.GetTextureDesc(badCube->GetHandle())->ArrayLayers == 6);
    const auto badImage = assets.LoadTexture("cube.ktx2");
    REQUIRE(badImage);
    CHECK(device.GetTextureDesc(badImage->GetHandle())->Extent.Width == 1);
    const auto missingCooker = assets.LoadTexture("uncooked.png");
    REQUIRE(missingCooker);
    CHECK(missingCooker == badImage);
    const auto uploads = context->GetStats().Uploads;
    CHECK(assets.LoadTexture("../escaped.png") == badImage);
    CHECK(context->GetStats().Uploads == uploads);
}

TEST_CASE("Registry cooks one descriptor cubemap and retains all face mip coverage",
          "[rhi][gpu][resources][acquisition]")
{
    auto& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);
    TextureCookFixture files;
    files.WriteCubemap();
    Platform::Paths paths(files.Content().string());
    const auto context = device.CreateUploadContext({});
    const Rhi::UniqueHandle<Rhi::SamplerHandle> sampler(
        device, device.CreateSampler({.DebugName = "Cube sampler"}));
    MaterialFactory materials(device, sampler.Get());
    AssetRegistry assets(device, *context, paths, materials, files.Cooked(), HIKARI_RELEASE_COOKER);
    const auto cube = assets.LoadCubemap("sky/sky.cubemap.json", "Authored cube");
    REQUIRE(cube);
    CHECK(device.GetTextureDesc(cube->GetHandle())->Format == Rhi::Format::BC1Srgb);
    CHECK(device.GetTextureDesc(cube->GetHandle())->MipLevels == 3);
    CHECK(device.GetTextureDesc(cube->GetHandle())->ArrayLayers == 6);
    CHECK(context->GetStats().Uploads == 1);
    CHECK(context->GetStats().Bytes == 6 * 3 * 8);
    CHECK(std::filesystem::is_regular_file(files.Cooked() / "sky/sky.cubemap.ktx2"));
}
