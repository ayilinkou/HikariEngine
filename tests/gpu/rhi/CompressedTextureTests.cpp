#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <cstddef>
#include <stdexcept>

#include <rhi/ICommandList.h>
#include <rhi/IDevice.h>
#include <rhi/UniqueHandle.h>

#include "RhiTestFixture.h"
#include "ValidationGuard.h"

using namespace Hikari::Rhi;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("BC device support agrees with texture creation and full-chain views",
          "[rhi][gpu][resources][bc]")
{
    IDevice& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);
    CHECK_FALSE(device.IsFormatSupported(Format::Undefined, TextureUsage::Sampled));
    for (const Format format : kAllFormats)
    {
        if (!IsBlockCompressed(format))
            continue;
        CAPTURE(format);
        constexpr TextureUsage kUsage =
            TextureUsage::Sampled | TextureUsage::CopyDst | TextureUsage::CopySrc;
        TextureDesc desc{.Format = format,
                         .Extent = {12u, 12u, 1u},
                         .MipLevels = 4u,
                         .Usage = kUsage,
                         .DebugName = "BC capability test"};
        if (!device.IsFormatSupported(format, kUsage))
        {
            CHECK_THROWS_WITH(device.CreateTexture(desc), ContainsSubstring("does not support"));
            continue;
        }
        for (const bool bCube : {false, true})
        {
            desc.bCubeCompatible = bCube;
            desc.ArrayLayers = bCube ? 6u : 1u;
            const UniqueHandle<TextureHandle> texture(device, device.CreateTexture(desc));
            const UniqueHandle<TextureViewHandle> view(
                device,
                device.CreateTextureView({.Texture = texture.Get(),
                                          .Dimension = bCube ? TextureViewDimension::TextureCube
                                                             : TextureViewDimension::Texture2D,
                                          .Format = format,
                                          .MipCount = 4u,
                                          .LayerCount = desc.ArrayLayers}));
            CHECK(device.GetTextureDesc(texture.Get())->Extent.Width == 12u);
        }
        CHECK_FALSE(device.IsFormatSupported(format, TextureUsage::ColorAttachment));
        CHECK_FALSE(device.IsFormatSupported(format, TextureUsage::Storage));
        desc.Usage = TextureUsage::ColorAttachment;
        CHECK_THROWS_WITH(device.CreateTexture(desc), ContainsSubstring("does not support"));
    }
}

TEST_CASE("Unsupported BC copies fail before staging or native recording",
          "[rhi][gpu][resources][upload][bc]")
{
    IDevice& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);
    constexpr TextureUsage kUsage =
        TextureUsage::Sampled | TextureUsage::CopyDst | TextureUsage::CopySrc;
    if (!device.IsFormatSupported(Format::BC1Unorm, kUsage))
        SKIP("Device has no BC1 texture support.");
    const UniqueHandle<TextureHandle> texture(
        device, device.CreateTexture({.Format = Format::BC1Unorm,
                                      .Extent = {4u, 4u, 1u},
                                      .Usage = kUsage,
                                      .DebugName = "BC unsupported transfer"}));
    const auto context = device.CreateUploadContext({});
    const std::array<std::byte, 8u> block{};
    const uint32_t buffersBefore = device.GetLiveBufferCount();
    CHECK_THROWS_WITH(
        context->UploadTexture(texture.Get(), TextureUpload{.Data = block, .Extent = {4u, 4u, 1u}}),
        ContainsSubstring("compressed texture transfers"));
    CHECK(context->GetStats().Uploads == 0u);
    CHECK(device.GetLiveBufferCount() == buffersBefore);

    const UniqueHandle<BufferHandle> buffer(
        device,
        device.CreateBuffer({.Size = 64u, .Usage = BufferUsage::CopySrc | BufferUsage::CopyDst}));
    const auto allocator = device.CreateCommandAllocator({});
    ICommandList& commands = allocator->Acquire();
    commands.Begin();
    const BufferTextureCopyRegion region{.Extent = {4u, 4u, 1u}};
    CHECK_THROWS_WITH(commands.CopyBufferToTexture(buffer.Get(), texture.Get(), region),
                      ContainsSubstring("compressed texture transfers"));
    CHECK_THROWS_WITH(commands.CopyTextureToBuffer(texture.Get(), buffer.Get(), region),
                      ContainsSubstring("compressed texture transfers"));
    commands.End();
}

TEST_CASE("Malformed uploads leave the staging context usable", "[rhi][gpu][upload]")
{
    IDevice& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);
    const UniqueHandle<TextureHandle> texture(
        device, device.CreateTexture({.Format = Format::RGBA8Unorm,
                                      .Extent = {2u, 2u, 1u},
                                      .Usage = TextureUsage::Sampled | TextureUsage::CopyDst}));
    const auto context = device.CreateUploadContext({});
    const std::array<std::byte, 16u> pixels{};
    const uint32_t buffersBefore = device.GetLiveBufferCount();
    CHECK_THROWS_WITH(
        context->UploadTexture(texture.Get(), TextureUpload{.Data = std::span(pixels).first(4),
                                                            .Extent = {2u, 2u, 1u}}),
        ContainsSubstring("payload size"));
    CHECK(device.GetLiveBufferCount() == buffersBefore);
    CHECK(context->GetStats().Uploads == 0u);
    context->UploadTexture(texture.Get(), TextureUpload{.Data = pixels, .Extent = {2u, 2u, 1u}});
    context->Flush();
    CHECK(context->GetStats().Uploads == 1u);
}

TEST_CASE("D3D12 rejects incompatible BC base dimensions without resizing",
          "[rhi][gpu][resources][d3d12][bc]")
{
    IDevice& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);
    constexpr TextureUsage kUsage = TextureUsage::Sampled | TextureUsage::CopyDst;
    if (!device.IsFormatSupported(Format::BC7Unorm, kUsage))
        SKIP("Device has no BC7 texture support.");
    for (const uint32_t width : {1u, 3u, 7u})
        CHECK_THROWS_WITH(
            device.CreateTexture(
                {.Format = Format::BC7Unorm, .Extent = {width, 4u, 1u}, .Usage = kUsage}),
            ContainsSubstring("multiples of 4"));
}
