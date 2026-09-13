#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstring>

#include <rhi/BufferDesc.h>
#include <rhi/SamplerDesc.h>
#include <rhi/TextureDesc.h>
#include <rhi/TextureViewDesc.h>
#include <rhi/IDevice.h>

#include "RhiTestFixture.h"
#include "ValidationGuard.h"

/**
 * What creating and destroying a resource promises on its own, before anything
 * records against it or submits it — the part of the resource contract a backend
 * can be held to before it has command lists.
 */
using namespace Hikari::Rhi;

TEST_CASE("A host-visible buffer is mapped for its life, and a GPU-only one never is",
          "[rhi][gpu][resources]")
{
    IDevice& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);
    const uint32_t buffersBefore = device.GetLiveBufferCount();

    const BufferHandle upload = device.CreateBuffer(BufferDesc{.Size = 256u,
                                                               .Usage = BufferUsage::Uniform,
                                                               .Access = MemoryAccess::CpuToGpu,
                                                               .DebugName = "Test Upload"});
    const BufferHandle readback = device.CreateBuffer(BufferDesc{.Size = 64u,
                                                                 .Usage = BufferUsage::CopyDst,
                                                                 .Access = MemoryAccess::GpuToCpu,
                                                                 .DebugName = "Test Readback"});
    const BufferHandle local = device.CreateBuffer(
        BufferDesc{.Size = 1024u,
                   .Usage = BufferUsage::Vertex | BufferUsage::Storage | BufferUsage::CopyDst,
                   .Access = MemoryAccess::GpuOnly,
                   .DebugName = "Test Local"});

    CHECK(device.GetLiveBufferCount() == buffersBefore + 3u);

    void* pUpload = device.GetMappedData(upload);
    REQUIRE(pUpload != nullptr);
    CHECK(device.GetMappedData(readback) != nullptr);
    CHECK(device.GetMappedData(local) == nullptr);

    // Writable across its whole size, which a mapping of the wrong length would not be.
    std::array<uint8_t, 256> pattern{};
    for (size_t i = 0; i < pattern.size(); ++i)
        pattern[i] = static_cast<uint8_t>(i);
    std::memcpy(pUpload, pattern.data(), pattern.size());
    CHECK(std::memcmp(pUpload, pattern.data(), pattern.size()) == 0);

    device.Destroy(upload);
    device.Destroy(readback);
    device.Destroy(local);

    CHECK(device.GetLiveBufferCount() == buffersBefore);
    CHECK(device.GetMappedData(upload) == nullptr);
}

TEST_CASE("Textures of every shape the renderer makes are created, described and destroyed",
          "[rhi][gpu][resources]")
{
    IDevice& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);

    const uint32_t texturesBefore = device.GetLiveTextureCount();
    const uint32_t viewsBefore = device.GetLiveTextureViewCount();
    const uint32_t samplersBefore = device.GetLiveSamplerCount();

    const TextureDesc colorDesc{.Format = Format::RGBA8Unorm,
                                .Extent = {64u, 32u, 1u},
                                .MipLevels = 3u,
                                .Usage = TextureUsage::Sampled | TextureUsage::CopyDst,
                                .DebugName = "Test Color"};
    const TextureHandle color = device.CreateTexture(colorDesc);

    const TextureHandle cube = device.CreateTexture(
        TextureDesc{.Format = Format::RGBA8Srgb,
                    .Extent = {16u, 16u, 1u},
                    .ArrayLayers = 6u,
                    .Usage = TextureUsage::Sampled | TextureUsage::CopyDst,
                    .bCubeCompatible = true,
                    .DebugName = "Test Cube"});

    const TextureHandle volume =
        device.CreateTexture(TextureDesc{.Dimension = TextureDimension::Texture3D,
                                         .Format = Format::RGBA16Float,
                                         .Extent = {8u, 8u, 8u},
                                         .Usage = TextureUsage::Sampled | TextureUsage::CopyDst,
                                         .DebugName = "Test Volume"});

    // A depth target a shader also reads, which is the one case whose resource and
    // views name different formats on D3D12.
    REQUIRE(device.IsFormatSupported(Format::D32Float, TextureUsage::DepthStencilAttachment |
                                                           TextureUsage::Sampled));
    const TextureHandle depth = device.CreateTexture(
        TextureDesc{.Format = Format::D32Float,
                    .Extent = {64u, 32u, 1u},
                    .Usage = TextureUsage::DepthStencilAttachment | TextureUsage::Sampled,
                    .DebugName = "Test Depth"});

    CHECK(device.GetLiveTextureCount() == texturesBefore + 4u);

    const TextureDesc* pColor = device.GetTextureDesc(color);
    REQUIRE(pColor != nullptr);
    CHECK(pColor->Extent.Width == 64u);
    CHECK(pColor->MipLevels == 3u);
    CHECK(pColor->Format == Format::RGBA8Unorm);

    const TextureViewHandle colorView = device.CreateTextureView(
        TextureViewDesc{.Texture = color, .Format = Format::RGBA8Unorm, .MipCount = 3u,
                        .DebugName = "Test Color View"});
    const TextureViewHandle cubeView =
        device.CreateTextureView(TextureViewDesc{.Texture = cube,
                                                 .Dimension = TextureViewDimension::TextureCube,
                                                 .Format = Format::RGBA8Srgb,
                                                 .LayerCount = 6u,
                                                 .DebugName = "Test Cube View"});
    const TextureViewHandle volumeView =
        device.CreateTextureView(TextureViewDesc{.Texture = volume,
                                                 .Dimension = TextureViewDimension::Texture3D,
                                                 .Format = Format::RGBA16Float,
                                                 .DebugName = "Test Volume View"});
    const TextureViewHandle depthView = device.CreateTextureView(TextureViewDesc{
        .Texture = depth, .Format = Format::D32Float, .Aspect = TextureAspect::Depth,
        .DebugName = "Test Depth View"});

    const SamplerHandle sampler = device.CreateSampler(SamplerDesc{});

    CHECK(device.GetLiveTextureViewCount() == viewsBefore + 4u);
    CHECK(device.GetLiveSamplerCount() == samplersBefore + 1u);

    device.Destroy(sampler);
    for (const TextureViewHandle view : {colorView, cubeView, volumeView, depthView})
        device.Destroy(view);
    for (const TextureHandle texture : {color, cube, volume, depth})
        device.Destroy(texture);

    CHECK(device.GetLiveTextureCount() == texturesBefore);
    CHECK(device.GetLiveTextureViewCount() == viewsBefore);
    CHECK(device.GetLiveSamplerCount() == samplersBefore);
    CHECK(device.GetTextureDesc(color) == nullptr);
}

TEST_CASE("Destroying a buffer twice is reported, not ignored", "[rhi][gpu][resources]")
{
    IDevice& device = RhiTest::RequireDevice();
    Diagnostics& diagnostics = device.GetDiagnostics();
    diagnostics.Reset();

    const BufferHandle buffer = device.CreateBuffer(BufferDesc{.Size = 16u,
                                                               .Usage = BufferUsage::Uniform,
                                                               .Access = MemoryAccess::CpuToGpu,
                                                               .DebugName = "Test Twice"});
    device.Destroy(buffer);
    CHECK(diagnostics.ErrorCount() == 0u);

    // The use-after-free detection the handle model exists to buy.
    device.Destroy(buffer);
    CHECK(diagnostics.ErrorCount() == 1u);

    diagnostics.Reset();
}
