#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <rhi/BarrierPresets.h>
#include <rhi/IDevice.h>
#include <rhi/TextureFootprint.h>
#include <rhi/UniqueHandle.h>

#include "GpuReadback.h"
#include "RhiTestFixture.h"
#include "ValidationGuard.h"

using namespace Hikari::Core;
using namespace Hikari::Rhi;
using Catch::Matchers::ContainsSubstring;

namespace
{
constexpr TextureUsage kUsage =
    TextureUsage::Sampled | TextureUsage::CopyDst | TextureUsage::CopySrc;

/** Different bytes per block, row, mip and face reveal misplaced or padded copies. */
std::vector<std::byte> Blocks(size_t byteCount, uint32_t seed)
{
    std::vector<std::byte> bytes(byteCount);
    for (std::byte& value : bytes)
    {
        seed = seed * 1664525u + 1013904223u;
        value = static_cast<std::byte>((seed >> 16u) & 0xFFu);
    }
    return bytes;
}

struct MipChain
{
    std::vector<std::vector<std::byte>> Data;
    std::vector<TextureUpload> Uploads;
    uint64_t Bytes = 0u;
};

/** Mix single-face and grouped-face regions, then submit them out of mip/face order. */
MipChain MakeChain(const TextureDesc& desc)
{
    MipChain chain;
    chain.Data.resize(desc.MipLevels);
    for (uint32_t mip = 0u; mip < desc.MipLevels; ++mip)
    {
        const Extent3D extent{std::max(desc.Extent.Width >> mip, 1u),
                              std::max(desc.Extent.Height >> mip, 1u), 1u};
        const size_t faceBytes =
            static_cast<size_t>(ComputeTextureFootprint(desc.Format, extent).TotalBytes);
        auto& data = chain.Data[mip];
        data = Blocks(faceBytes * desc.ArrayLayers, 100u + mip);
        chain.Bytes += data.size();
        if (mip % 2u == 0u)
            chain.Uploads.push_back(
                {.Data = data, .MipLevel = mip, .LayerCount = desc.ArrayLayers, .Extent = extent});
        else
            for (uint32_t face = 0u; face < desc.ArrayLayers; ++face)
                chain.Uploads.push_back(
                    {.Data = std::span(data).subspan(faceBytes * face, faceBytes),
                     .MipLevel = mip,
                     .BaseLayer = face,
                     .Extent = extent});
    }
    std::reverse(chain.Uploads.begin(), chain.Uploads.end());
    return chain;
}

void CheckChain(IDevice& device, TextureHandle texture, const MipChain& chain)
{
    for (uint32_t mip = 0u; mip < chain.Data.size(); ++mip)
    {
        CAPTURE(mip);
        const auto layers = RhiTest::ReadTextureLayers(device, texture, mip);
        std::vector<std::byte> joined;
        for (const auto& layer : layers)
            joined.insert(joined.end(), layer.begin(), layer.end());
        CHECK(joined == chain.Data[mip]);
    }
}
} // namespace

TEST_CASE("Every BC format round-trips non-power-of-two mip chains and cubemap faces",
          "[rhi][gpu][upload][bc]")
{
    for (const auto config : RhiTest::AllDeviceConfigs())
    {
        INFO(RhiTest::Describe(config));
        IDevice& device = RhiTest::RequireDevice(config);
        const RhiTest::ValidationGuard guard(device);
        const uint32_t buffersBefore = device.GetLiveBufferCount();
        const uint32_t texturesBefore = device.GetLiveTextureCount();
        uint32_t tested = 0u;
        for (const Format format : kAllFormats)
        {
            if (!IsBlockCompressed(format) || !device.IsFormatSupported(format, kUsage))
                continue;
            ++tested;
            for (const bool bCube : {false, true})
            {
                CAPTURE(format, bCube);
                const TextureDesc desc{.Format = format,
                                       .Extent = {12u, bCube ? 12u : 20u, 1u},
                                       .MipLevels = bCube ? 4u : 5u,
                                       .ArrayLayers = bCube ? 6u : 1u,
                                       .Usage = kUsage,
                                       .bCubeCompatible = bCube,
                                       .DebugName = "BC mip round-trip"};
                const MipChain chain = MakeChain(desc);
                const UniqueHandle<TextureHandle> texture(device, device.CreateTexture(desc));
                const auto context = device.CreateUploadContext({.StagingBudget = 1u});
                context->UploadTexture(texture.Get(), chain.Uploads);
                context->Flush();
                CHECK(context->GetStats().Uploads == 1u);
                CHECK(context->GetStats().Batches == 1u);
                CHECK(context->GetStats().Bytes == chain.Bytes);
                CheckChain(device, texture.Get(), chain);
            }
        }
        if (tested == 0u)
            SKIP("Device has no supported BC formats.");
        CHECK(device.GetLiveBufferCount() == buffersBefore);
        CHECK(device.GetLiveTextureCount() == texturesBefore);
    }
}

TEST_CASE("BC uploads keep full mip chains intact at staging budget boundaries",
          "[rhi][gpu][upload][bc]")
{
    for (const auto config : RhiTest::AllDeviceConfigs())
    {
        INFO(RhiTest::Describe(config));
        IDevice& device = RhiTest::RequireDevice(config);
        const RhiTest::ValidationGuard guard(device);
        for (const Format format : {Format::BC1Unorm, Format::BC7Unorm})
        {
            if (!device.IsFormatSupported(format, kUsage))
                SKIP("Device has no BC1/BC7 support.");
            const TextureDesc desc{
                .Format = format, .Extent = {12u, 12u, 1u}, .MipLevels = 4u, .Usage = kUsage};
            const MipChain chain = MakeChain(desc);
            for (const uint64_t budget :
                 {chain.Bytes * 2u, chain.Bytes * 2u - 1u, chain.Bytes - 1u})
            {
                CAPTURE(format, budget);
                const UniqueHandle<TextureHandle> first(device, device.CreateTexture(desc));
                const UniqueHandle<TextureHandle> second(device, device.CreateTexture(desc));
                const auto context = device.CreateUploadContext({.StagingBudget = budget});
                context->UploadTexture(first.Get(), chain.Uploads);
                CHECK(context->GetStats().Batches == 0u);
                context->UploadTexture(second.Get(), chain.Uploads);
                const uint64_t autoBatches = budget < chain.Bytes * 2u ? 1u : 0u;
                CHECK(context->GetStats().Batches == autoBatches);
                context->Flush();
                CHECK(context->GetStats().Batches == autoBatches + 1u);
                CHECK(context->GetStats().Uploads == 2u);
                CHECK(context->GetStats().Bytes == chain.Bytes * 2u);
                CheckChain(device, first.Get(), chain);
                CheckChain(device, second.Get(), chain);
            }
        }
    }
}

TEST_CASE("BC direct copies pack face ranges at nonzero block offsets", "[rhi][gpu][upload][bc]")
{
    IDevice& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);
    for (const Format format : {Format::BC1Unorm, Format::BC7Unorm})
    {
        if (!device.IsFormatSupported(format, kUsage))
            SKIP("Device has no BC1/BC7 support.");
        CAPTURE(format);
        const TextureDesc desc{.Format = format,
                               .Extent = {12u, 12u, 1u},
                               .ArrayLayers = 6u,
                               .Usage = kUsage,
                               .bCubeCompatible = true};
        const size_t blockBytes = BytesPerBlock(format);
        const size_t faceBytes = blockBytes * 9u;
        const size_t offset = blockBytes * 3u;
        const auto source = Blocks(offset + faceBytes * 6u, 999u);
        const UniqueHandle<TextureHandle> texture(device, device.CreateTexture(desc));
        const UniqueHandle<BufferHandle> staging(
            device, device.CreateBuffer({.Size = source.size(),
                                         .Usage = BufferUsage::CopySrc,
                                         .Access = MemoryAccess::CpuToGpu}));
        std::memcpy(device.GetMappedData(staging.Get()), source.data(), source.size());
        const UniqueHandle<BufferHandle> readback(
            device, device.CreateBuffer({.Size = source.size(),
                                         .Usage = BufferUsage::CopyDst,
                                         .Access = MemoryAccess::GpuToCpu}));
        RhiTest::RunGraphicsCommands(
            device,
            [&](ICommandList& list)
            {
                list.Barrier(BarrierPresets::UndefinedToCopyDst(6u).On(texture.Get()));
                list.CopyBufferToTexture(
                    staging.Get(), texture.Get(),
                    {.BufferOffset = offset, .LayerCount = 6u, .Extent = desc.Extent});
                list.Barrier(TextureBarrier{.Texture = texture.Get(),
                                            .SrcStage = PipelineStage::Copy,
                                            .SrcAccess = AccessFlags::CopyDst,
                                            .DstStage = PipelineStage::Copy,
                                            .DstAccess = AccessFlags::CopySrc,
                                            .OldLayout = TextureLayout::CopyDst,
                                            .NewLayout = TextureLayout::CopySrc,
                                            .LayerCount = 6u});
                // Read faces 2..4 as one packed region. Using all faces would hide a
                // readback that ignores BaseLayer, and using one would hide layer stride bugs.
                list.CopyTextureToBuffer(texture.Get(), readback.Get(),
                                         {.BufferOffset = offset,
                                          .BaseLayer = 2u,
                                          .LayerCount = 3u,
                                          .Extent = desc.Extent});
            });
        const auto* pRead = static_cast<const std::byte*>(device.GetMappedData(readback.Get()));
        REQUIRE(pRead != nullptr);
        CHECK(std::equal(source.begin() + offset + 2u * faceBytes,
                         source.begin() + offset + 5u * faceBytes, pRead + offset));
    }
}

TEST_CASE("BC copies permit whole-block partial regions and reject split interior blocks",
          "[rhi][gpu][upload][bc]")
{
    IDevice& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);
    constexpr Format kFormat = Format::BC1Unorm;
    if (!device.IsFormatSupported(kFormat, kUsage))
        SKIP("Device has no BC1 support.");
    const TextureDesc desc{
        .Format = kFormat, .Extent = {12u, 12u, 1u}, .MipLevels = 2u, .Usage = kUsage};
    const MipChain chain = MakeChain(desc);
    const UniqueHandle<TextureHandle> texture(device, device.CreateTexture(desc));
    const auto context = device.CreateUploadContext({});
    context->UploadTexture(texture.Get(), chain.Uploads);
    context->Flush();
    const auto patch = Blocks(32u, 789u); // Two rows of two BC1 blocks.
    const UniqueHandle<BufferHandle> staging(
        device, device.CreateBuffer({.Size = patch.size(),
                                     .Usage = BufferUsage::CopySrc,
                                     .Access = MemoryAccess::CpuToGpu}));
    std::memcpy(device.GetMappedData(staging.Get()), patch.data(), patch.size());
    const UniqueHandle<BufferHandle> readback(
        device,
        device.CreateBuffer(
            {.Size = 32u, .Usage = BufferUsage::CopyDst, .Access = MemoryAccess::GpuToCpu}));
    RhiTest::RunGraphicsCommands(
        device,
        [&](ICommandList& list)
        {
            list.Barrier(TextureBarrier{.Texture = texture.Get(),
                                        .DstStage = PipelineStage::Copy,
                                        .DstAccess = AccessFlags::CopyDst,
                                        .OldLayout = TextureLayout::ShaderResource,
                                        .NewLayout = TextureLayout::CopyDst});
            CHECK_THROWS_WITH(
                list.CopyBufferToTexture(staging.Get(), texture.Get(),
                                         {.BufferOffset = 4u, .Extent = {4u, 4u, 1u}}),
                ContainsSubstring("texel block size"));
            CHECK_THROWS_WITH(
                list.CopyBufferToTexture(staging.Get(), texture.Get(), {.Extent = {6u, 4u, 1u}}),
                ContainsSubstring("whole blocks"));
            list.CopyBufferToTexture(staging.Get(), texture.Get(), {.Extent = {8u, 8u, 1u}});
            list.Barrier(TextureBarrier{.Texture = texture.Get(),
                                        .SrcStage = PipelineStage::Copy,
                                        .SrcAccess = AccessFlags::CopyDst,
                                        .DstStage = PipelineStage::Copy,
                                        .DstAccess = AccessFlags::CopySrc,
                                        .OldLayout = TextureLayout::CopyDst,
                                        .NewLayout = TextureLayout::CopySrc});
            CHECK_THROWS_WITH(
                list.CopyTextureToBuffer(texture.Get(), readback.Get(), {.Extent = {4u, 6u, 1u}}),
                ContainsSubstring("whole blocks"));
            list.CopyTextureToBuffer(texture.Get(), readback.Get(), {.Extent = {8u, 8u, 1u}});
        });
    const auto* pRead = static_cast<const std::byte*>(device.GetMappedData(readback.Get()));
    REQUIRE(pRead != nullptr);
    CHECK(std::equal(patch.begin(), patch.end(), pRead));

    // Mip 1 is logically 6x6. Its first block row reaches the right edge at
    // six texels but needs eight texels in D3D12's buffer footprint/source box.
    RhiTest::RunGraphicsCommands(
        device,
        [&](ICommandList& list)
        {
            list.Barrier(TextureBarrier{.Texture = texture.Get(),
                                        .DstStage = PipelineStage::Copy,
                                        .DstAccess = AccessFlags::CopySrc,
                                        .OldLayout = TextureLayout::ShaderResource,
                                        .NewLayout = TextureLayout::CopySrc,
                                        .BaseMip = 1u});
            list.CopyTextureToBuffer(texture.Get(), readback.Get(),
                                     {.BufferOffset = 8u, .MipLevel = 1u, .Extent = {6u, 4u, 1u}});
        });
    CHECK(std::equal(chain.Data[1].begin(), chain.Data[1].begin() + 16u, pRead + 8u));
}

TEST_CASE("Vulkan BC transfers retain odd base extents and tiny base levels",
          "[rhi][gpu][bc][vulkan]")
{
    for (const auto config : RhiTest::AllDeviceConfigs())
    {
        INFO(RhiTest::Describe(config));
        IDevice& device = RhiTest::RequireDevice(config);
        const RhiTest::ValidationGuard guard(device);
        if (!device.IsFormatSupported(Format::BC7Unorm, kUsage))
            SKIP("Device has no BC7 support.");
        for (const Extent3D extent : {Extent3D{7u, 5u, 1u}, Extent3D{1u, 1u, 1u}})
        {
            const TextureDesc desc{.Format = Format::BC7Unorm,
                                   .Extent = extent,
                                   .MipLevels = static_cast<uint32_t>(std::bit_width(extent.Width)),
                                   .Usage = kUsage};
            const MipChain chain = MakeChain(desc);
            const UniqueHandle<TextureHandle> texture(device, device.CreateTexture(desc));
            const auto context = device.CreateUploadContext({});
            context->UploadTexture(texture.Get(), chain.Uploads);
            context->Flush();
            CheckChain(device, texture.Get(), chain);
        }
    }
}
