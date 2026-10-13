#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include <assimp/material.h>

#include <asset/ImageWriter.h>
#include <asset/KtxReader.h>
#include <rhi/BarrierPresets.h>
#include <rhi/BufferDesc.h>
#include <rhi/IDevice.h>
#include <rhi/Pipeline.h>
#include <rhi/UniqueHandle.h>

#include "AssetRegistry.h"
#include "GpuReadback.h"
#include "KtxFixture.h"
#include "MaterialFactory.h"
#include "RhiTestFixture.h"
#include "Texture.h"
#include "TextureCookFixture.h"
#include "TextureSampling.h"
#include "ValidationGuard.h"
#include "Vertex.h"

using namespace Hikari;
using namespace Hikari::Rhi;

namespace
{
UniqueHandle<ShaderModuleHandle> LoadShader(IDevice& device, const std::string& name)
{
    const auto filename = name + "." + std::string(device.GetCaps().ShaderExtension);
    std::ifstream file(std::filesystem::path(HIKARI_KTX_SHADER_DIR) / filename, std::ios::binary);
    REQUIRE(file.is_open());
    const std::vector<char> code((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
    return {device, device.CreateShaderModule(
                        {.Bytes = std::as_bytes(std::span(code)), .DebugName = filename})};
}

Texture Upload(IDevice& device, IUploadContext& context, const std::filesystem::path& path)
{
    std::string error;
    const auto data = Asset::ReadKtxTexture(path, error);
    INFO(error);
    REQUIRE(data);
    Texture texture(device,
                    {.Format = data->Format,
                     .Extent = {data->Extent.Width, data->Extent.Height, 1u},
                     .MipLevels = data->MipLevels,
                     .Usage = TextureUsage::Sampled | TextureUsage::CopyDst,
                     .DebugName = "Surface source"},
                    TextureViewDimension::Texture2D, path.string(), data->Metadata);
    std::vector<TextureUpload> uploads;
    for (const auto& resource : data->Subresources)
        uploads.push_back({.Data = std::span(data->Data).subspan(resource.Offset, resource.Size),
                           .MipLevel = resource.MipLevel,
                           .Extent = resource.Extent});
    context.UploadTexture(texture.GetHandle(), uploads);
    context.Flush();
    return texture;
}

void FillBc1(std::vector<std::byte>& payload, uint16_t color, bool transparent = false)
{
    for (size_t offset = 0; offset < payload.size(); offset += 8)
    {
        payload[offset] = static_cast<std::byte>(color & 255u);
        payload[offset + 1] = static_cast<std::byte>(color >> 8u);
        payload[offset + 2] = payload[offset];
        payload[offset + 3] = payload[offset + 1];
        std::fill_n(payload.begin() + offset + 4, 4, transparent ? std::byte{255} : std::byte{0});
    }
}

void CheckPixel(const std::vector<std::byte>& pixels, size_t index,
                const std::array<float, 4>& expected, int tolerance = 2)
{
    for (size_t channel = 0; channel < expected.size(); ++channel)
    {
        CAPTURE(index, channel);
        const int actual = std::to_integer<int>(pixels.at(index * 4 + channel));
        const int wanted = static_cast<int>(std::lround(expected[channel] * 255.f));
        CHECK(std::abs(actual - wanted) <= tolerance);
    }
}

std::vector<std::byte> RenderSample(IDevice& device, const Texture& source, float uvScale)
{
    constexpr uint32_t kSize = 16;
    const Texture target(
        device,
        {.Format = Format::RGBA8Unorm,
         .Extent = {kSize, kSize, 1u},
         .Usage = TextureUsage::ColorAttachment | TextureUsage::Sampled | TextureUsage::CopySrc,
         .DebugName = "Implicit mip result"},
        TextureViewDimension::Texture2D);
    const UniqueHandle<SamplerHandle> sampler(device,
                                              device.CreateSampler(Engine::SurfaceSamplerDesc()));
    const std::array bindings{
        BindGroupLayoutBinding{
            .Slot = 0u, .Type = BindingType::Texture, .Visibility = ShaderStage::Pixel},
        BindGroupLayoutBinding{
            .Slot = 1u, .Type = BindingType::Sampler, .Visibility = ShaderStage::Pixel}};
    const UniqueHandle<BindGroupLayoutHandle> groupLayout(
        device, device.CreateBindGroupLayout({.Bindings = bindings, .DebugName = "Surface test"}));
    const std::array contents{
        BindGroupBinding{.Slot = 0u, .Type = BindingType::Texture, .View = source.GetView()},
        BindGroupBinding{.Slot = 1u, .Type = BindingType::Sampler, .Sampler = sampler.Get()}};
    const UniqueHandle<BindGroupHandle> group(
        device,
        device.CreateBindGroup(
            {.Layout = groupLayout.Get(), .Bindings = contents, .DebugName = "Surface test"}));
    const std::array layouts{groupLayout.Get()};
    const UniqueHandle<PipelineLayoutHandle> layout(
        device,
        device.CreatePipelineLayout({.BindGroupLayouts = layouts, .DebugName = "Surface test"}));
    const auto vertex = LoadShader(device, "SurfaceSample.vert");
    const auto pixel = LoadShader(device, "SurfaceSample.frag");
    const auto cache = device.CreatePipelineCache({.Path = {}, .DebugName = "Surface test"});
    const std::array formats{Format::RGBA8Unorm};
    const std::array blends{RenderTargetBlend{}};
    const std::array buffers{QuadVertex::GetBindingDescription()};
    const auto attributes = QuadVertex::GetAttributeDescription();
    const UniqueHandle<GraphicsPipelineHandle> pipeline(
        device, device.CreateGraphicsPipeline({.Layout = layout.Get(),
                                               .VertexShader = {.Module = vertex.Get()},
                                               .PixelShader = {.Module = pixel.Get()},
                                               .VertexBuffers = buffers,
                                               .VertexAttributes = attributes,
                                               .RenderTargetFormats = formats,
                                               .RenderTargetBlends = blends,
                                               .DebugName = "Surface test"},
                                              *cache));
    // One oversized triangle avoids a diagonal derivative seam. Isotropic UV
    // derivatives select log2(uvScale) with the production anisotropic sampler.
    const std::array vertices{QuadVertex{{-1.f, -1.f}, {0.f, 0.f}},
                              QuadVertex{{3.f, -1.f}, {2.f * uvScale, 0.f}},
                              QuadVertex{{-1.f, 3.f}, {0.f, 2.f * uvScale}}};
    constexpr std::array<uint32_t, 3> indices{0, 1, 2};
    const UniqueHandle<BufferHandle> vertexBuffer(
        device, device.CreateBuffer({.Size = sizeof(vertices),
                                     .Usage = BufferUsage::Vertex | BufferUsage::CopyDst,
                                     .DebugName = "Mip triangle"}));
    const UniqueHandle<BufferHandle> indexBuffer(
        device, device.CreateBuffer({.Size = sizeof(indices),
                                     .Usage = BufferUsage::Index | BufferUsage::CopyDst,
                                     .DebugName = "Mip indices"}));
    const auto context = device.CreateUploadContext({});
    context->UploadBuffer(vertexBuffer.Get(), 0, std::as_bytes(std::span(vertices)));
    context->UploadBuffer(indexBuffer.Get(), 0, std::as_bytes(std::span(indices)));
    context->Flush();
    const std::array targets{RenderTarget{.View = target.GetView(), .Load = LoadOp::Clear}};
    RhiTest::RunGraphicsCommands(
        device,
        [&](ICommandList& list)
        {
            list.Barrier(BarrierPresets::UndefinedToRenderTarget().On(target.GetHandle()));
            list.BeginRendering(
                {.RenderArea = {.Extent = {kSize, kSize}}, .RenderTargets = targets});
            list.SetViewport({.Width = float(kSize), .Height = float(kSize)});
            list.SetScissor({.Extent = {kSize, kSize}});
            list.SetPipeline(pipeline.Get());
            list.SetBindGroup(layout.Get(), 0, group.Get());
            list.SetVertexBuffer(0, vertexBuffer.Get());
            list.SetIndexBuffer(indexBuffer.Get(), IndexFormat::Uint32);
            list.DrawIndexed(3, 1, 0, 0, 0);
            list.EndRendering();
            list.Barrier(BarrierPresets::RenderTargetToShaderResource().On(target.GetHandle()));
        });
    return RhiTest::ReadTextureLayers(device, target.GetHandle()).at(0);
}

std::vector<std::byte> DecodeNormals(IDevice& device, const Texture& source)
{
    const auto* desc = device.GetTextureDesc(source.GetHandle());
    REQUIRE(desc);
    const Texture target(
        device,
        {.Format = Format::RGBA8Unorm,
         .Extent = {desc->MipLevels, 1u, 1u},
         .Usage = TextureUsage::Storage | TextureUsage::Sampled | TextureUsage::CopySrc,
         .DebugName = "Normal decoding result"},
        TextureViewDimension::Texture2D);
    const UniqueHandle<SamplerHandle> sampler(device,
                                              device.CreateSampler(Engine::SurfaceSamplerDesc()));
    const std::array bindings{
        BindGroupLayoutBinding{
            .Slot = 0u, .Type = BindingType::Texture, .Visibility = ShaderStage::Compute},
        BindGroupLayoutBinding{
            .Slot = 1u, .Type = BindingType::Sampler, .Visibility = ShaderStage::Compute},
        BindGroupLayoutBinding{.Slot = 2u,
                               .Type = BindingType::UnorderedAccessTexture,
                               .Visibility = ShaderStage::Compute}};
    const UniqueHandle<BindGroupLayoutHandle> groupLayout(
        device, device.CreateBindGroupLayout({.Bindings = bindings, .DebugName = "Normal test"}));
    const std::array contents{
        BindGroupBinding{.Slot = 0u, .Type = BindingType::Texture, .View = source.GetView()},
        BindGroupBinding{.Slot = 1u, .Type = BindingType::Sampler, .Sampler = sampler.Get()},
        BindGroupBinding{
            .Slot = 2u, .Type = BindingType::UnorderedAccessTexture, .View = target.GetView()}};
    const UniqueHandle<BindGroupHandle> group(
        device,
        device.CreateBindGroup(
            {.Layout = groupLayout.Get(), .Bindings = contents, .DebugName = "Normal test"}));
    const std::array layouts{groupLayout.Get()};
    const std::array push{
        PushConstantRange{.Stages = ShaderStage::Compute, .Size = sizeof(MaterialPushConstant)}};
    const UniqueHandle<PipelineLayoutHandle> layout(
        device,
        device.CreatePipelineLayout(
            {.BindGroupLayouts = layouts, .PushConstantRanges = push, .DebugName = "Normal test"}));
    const auto shader = LoadShader(device, "NormalDecode.comp");
    const auto cache = device.CreatePipelineCache({.Path = {}, .DebugName = "Normal test"});
    const UniqueHandle<ComputePipelineHandle> pipeline(
        device, device.CreateComputePipeline({.Layout = layout.Get(),
                                              .Shader = {.Module = shader.Get()},
                                              .DebugName = "Normal test"},
                                             *cache));
    MaterialPushConstant material;
    material.NormalEncoding = Engine::NormalShaderEncoding(source.GetMetadata(), desc->Format);
    RhiTest::RunGraphicsCommands(
        device,
        [&](ICommandList& list)
        {
            list.Barrier(BarrierPresets::UndefinedToUnorderedAccess().On(target.GetHandle()));
            list.SetPipeline(pipeline.Get());
            list.SetComputeBindGroup(layout.Get(), 0, group.Get());
            list.PushConstants(layout.Get(), ShaderStage::Compute, 0,
                               std::as_bytes(std::span(&material, 1)));
            list.Dispatch(desc->MipLevels, 1, 1);
            list.Barrier(
                BarrierPresets::UnorderedAccessToShaderResource(PipelineStage::ComputeStage)
                    .On(target.GetHandle()));
        });
    return RhiTest::ReadTextureLayers(device, target.GetHandle()).at(0);
}
} // namespace

TEST_CASE("The surface sampler selects smaller mips and accepts base-only textures",
          "[rhi][gpu][rendering][surface-texture]")
{
    auto& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);
    TextureCookFixture files;
    const auto context = device.CreateUploadContext({});
    constexpr std::array<uint16_t, 5> colors{0xF800, 0x07E0, 0x001F, 0xFFE0, 0x07FF};
    for (uint32_t levels : {5u, 1u})
    {
        const auto fixture =
            MakeKtxFixture(133, 16, 16, levels, 1, {}, [&](uint32_t mip, uint32_t, auto& payload)
                           { FillBc1(payload, colors[mip]); });
        const auto path = files.Root() / "mips.ktx2";
        TextureCookFixture::WriteText(path, fixture.Bytes);
        const auto source = Upload(device, *context, path);
        for (float scale : {1.f, 4.f, 32.f})
        {
            CAPTURE(levels, scale);
            const auto pixels = RenderSample(device, source, scale);
            const uint32_t mip = levels == 1 ? 0 : scale == 1.f ? 0 : scale == 4.f ? 2 : 4;
            const uint16_t color = colors[mip];
            const std::array<float, 4> expected{color & 0xF800 ? 1.f : 0.f,
                                                color & 0x07E0 ? 1.f : 0.f,
                                                color & 0x001F ? 1.f : 0.f, 1.f};
            for (size_t index = 0; index < 16 * 16; ++index)
                CheckPixel(pixels, index, expected);
        }
    }
}

TEST_CASE("Cooked color data and retained masked blocks keep their sampling interpretation",
          "[rhi][gpu][rendering][surface-texture]")
{
    auto& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);
    TextureCookFixture files;
    const auto context = device.CreateUploadContext({});
    constexpr uint16_t color = (16u << 11u) | (32u << 5u) | 16u;
    const auto linearize = [](float value)
    { return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f); };
    for (bool srgb : {false, true})
    {
        const auto fixture = MakeKtxFixture(
            srgb ? 134 : 133, 16, 16, 1, 1, {{"HikariTextureUsage", srgb ? "color" : "packed"}},
            [&](uint32_t, uint32_t, auto& payload) { FillBc1(payload, color); });
        const auto path = files.Root() / "color.ktx2";
        TextureCookFixture::WriteText(path, fixture.Bytes);
        const auto source = Upload(device, *context, path);
        const auto pixels = RenderSample(device, source, 1.f);
        const float redBlue = 132.f / 255.f;
        const float green = 130.f / 255.f;
        CheckPixel(pixels, 8 * 16 + 8,
                   {srgb ? linearize(redBlue) : redBlue, srgb ? linearize(green) : green,
                    srgb ? linearize(redBlue) : redBlue, 1.f});
    }
    const auto masked =
        MakeKtxFixture(134, 16, 16, 1, 1,
                       {{"HikariTextureUsage", "color"},
                        {"HikariTextureAlphaMode", "mask"},
                        {"HikariTextureAlphaCutoff", "0.5"}},
                       [&](uint32_t, uint32_t, auto& payload)
                       {
                           FillBc1(payload, 0x07E0);
                           for (size_t block = 0; block < payload.size() / 8; ++block)
                               if ((block % 4) < 2)
                                   std::fill_n(payload.begin() + block * 8 + 4, 4, std::byte{255});
                       });
    const auto path = files.Root() / "mask.ktx2";
    TextureCookFixture::WriteText(path, masked.Bytes);
    const auto source = Upload(device, *context, path);
    const auto pixels = RenderSample(device, source, 1.f);
    // Probe away from filtering boundaries; no alpha-coverage claim is made.
    for (uint32_t y : {3u, 12u})
    {
        CheckPixel(pixels, y * 16 + 3, {0.f, 0.f, 0.f, 0.f});
        CheckPixel(pixels, y * 16 + 12, {0.f, 1.f, 0.f, 1.f});
    }
}

TEST_CASE("The shared normal decoder preserves signed axes swizzles and positive Z at every mip",
          "[rhi][gpu][rendering][surface-texture]")
{
    auto& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);
    TextureCookFixture files;
    const auto context = device.CreateUploadContext({});
    for (const std::string encoding : {"xy-unorm-positive-z", "xy-snorm-positive-z",
                                       "yx-unorm-positive-z", "ag-unorm-positive-z", "unknown"})
    {
        const bool signedXy = encoding == "xy-snorm-positive-z";
        const bool swapped = encoding == "yx-unorm-positive-z";
        const bool alphaGreen = encoding == "ag-unorm-positive-z";
        std::vector<std::pair<std::string, std::string>> metadata;
        if (encoding != "unknown")
            metadata = {{"HikariTextureUsage", "normal"}, {"HikariNormalEncoding", encoding}};
        if (swapped || alphaGreen)
            metadata.emplace_back("HikariDdsSwizzle", swapped ? "A2XY" : "A2D5");
        const auto fixture = MakeKtxFixture(
            alphaGreen ? 137
            : signedXy ? 142
                       : 141,
            16, 16, 5, 1, metadata,
            [&](uint32_t mip, uint32_t, auto& payload)
            {
                uint8_t x = signedXy ? 76 : 204;
                uint8_t y = signedXy ? static_cast<uint8_t>(-76) : 51;
                if (mip == 1)
                    std::swap(x, y);
                if (mip == 2)
                    x = y = signedXy ? 0 : 128;
                if (mip >= 3)
                    x = y = signedXy ? 127 : 255;
                if (swapped)
                    std::swap(x, y);
                for (size_t offset = 0; offset < payload.size(); offset += 16)
                {
                    std::fill_n(payload.begin() + offset, 16, std::byte{0});
                    payload[offset] = payload[offset + 1] = static_cast<std::byte>(x);
                    if (alphaGreen)
                    {
                        // BC3 green endpoint: mip 0/1 have fixed opposing X,
                        // green around zero; mip 2 flat, then an overlong XY.
                        const uint16_t green = (mip >= 3 ? 63u : 32u) << 5u;
                        payload[offset + 8] = payload[offset + 10] =
                            static_cast<std::byte>(green & 255u);
                        payload[offset + 9] = payload[offset + 11] =
                            static_cast<std::byte>(green >> 8u);
                    }
                    else
                        payload[offset + 8] = payload[offset + 9] = static_cast<std::byte>(y);
                }
            });
        const auto path = files.Root() / "normal.ktx2";
        TextureCookFixture::WriteText(path, fixture.Bytes);
        const auto source = Upload(device, *context, path);
        const auto pixels = DecodeNormals(device, source);
        for (uint32_t mip = 0; mip < 5; ++mip)
        {
            CAPTURE(encoding, mip);
            float x = signedXy ? 76.f / 127.f : 0.6f;
            float y = -x;
            if (mip == 1)
                std::swap(x, y);
            if (mip == 2)
                x = y = signedXy ? 0.f : 1.f / 255.f;
            if (mip >= 3)
                x = y = 1.f;
            if (alphaGreen)
                y = mip >= 3 ? 1.f : 130.f / 255.f * 2.f - 1.f;
            const float z = std::sqrt(std::max(0.f, 1.f - x * x - y * y));
            const float length = std::sqrt(x * x + y * y + z * z);
            CheckPixel(pixels, mip,
                       {x / length * 0.5f + 0.5f, y / length * 0.5f + 0.5f,
                        z / length * 0.5f + 0.5f, 1.f});
        }
    }
}

TEST_CASE("Materials retain explicit BC7 RGB normal decoding and select native BC5 decoding",
          "[rhi][gpu][resources][surface-texture]")
{
    auto& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);
    TextureCookFixture files;
    const auto path = files.Source("normal.png");
    std::vector<uint8_t> pixels(4 * 4 * 4);
    for (size_t offset = 0; offset < pixels.size(); offset += 4)
        std::copy_n(std::array<uint8_t, 4>{204, 51, 195}.begin(), 4, pixels.begin() + offset);
    REQUIRE(Asset::WritePng(pixels, {4, 4}, path.string()));
    TextureCookFixture::WriteText(path.string() + ".texture.json",
                                  R"({"version":1,"usage":"normal","format":"bc7"})");
    Platform::Paths paths(files.Content().string());
    const auto context = device.CreateUploadContext({});
    const UniqueHandle<SamplerHandle> sampler(device,
                                              device.CreateSampler(Engine::SurfaceSamplerDesc()));
    MaterialFactory materials(device, sampler.Get());
    AssetRegistry assets(device, *context, paths, materials, files.Cooked(), HIKARI_RELEASE_COOKER);
    aiMaterial imported;
    aiString normalPath("normal.png");
    REQUIRE(imported.AddProperty(&normalPath, AI_MATKEY_TEXTURE_NORMALS(0)) == AI_SUCCESS);
    PBRMaterial material(device, materials.GetLayout(), sampler.Get(), &imported,
                         files.Content().string() + "/", assets);
    const auto* constants =
        static_cast<const MaterialPushConstant*>(material.GetPushConstantData());
    REQUIRE(constants->bHasNormalTex);
    CHECK(constants->NormalEncoding == NormalEncodingXyzUnorm);
    const auto texture = assets.LoadTexture("normal.png", Asset::TextureUsage::Normal);
    REQUIRE(texture);
    CHECK(device.GetTextureDesc(texture->GetHandle())->Format == Format::BC7Unorm);
    const auto decoded = DecodeNormals(device, *texture);
    for (size_t mip = 0; mip < decoded.size() / 4; ++mip)
        CheckPixel(decoded, mip, {0.8f, 0.2f, 0.765f, 1.f}, 5);

    for (bool signedXy : {false, true})
    {
        const auto fixture = MakeKtxFixture(signedXy ? 142 : 141, 4, 4, 1);
        const auto name = signedXy ? "signed.ktx2" : "unsigned.ktx2";
        TextureCookFixture::WriteText(files.Cooked() / name, fixture.Bytes);
        aiMaterial native;
        aiString nativePath(name);
        REQUIRE(native.AddProperty(&nativePath, AI_MATKEY_TEXTURE_NORMALS(0)) == AI_SUCCESS);
        PBRMaterial nativeMaterial(device, materials.GetLayout(), sampler.Get(), &native, "",
                                   assets);
        const auto* nativeConstants =
            static_cast<const MaterialPushConstant*>(nativeMaterial.GetPushConstantData());
        REQUIRE(nativeConstants->bHasNormalTex);
        CHECK(nativeConstants->NormalEncoding ==
              (signedXy ? NormalEncodingXySnormPositiveZ : NormalEncodingXyUnormPositiveZ));
    }
}

TEST_CASE("Degenerate and non-finite RGB normals decode to the flat tangent normal",
          "[rhi][gpu][rendering][surface-texture]")
{
    auto& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);
    const auto context = device.CreateUploadContext({});
    const std::array samples{glm::vec4(0.5f, 0.5f, 0.5f, 1.f),
                             glm::vec4(std::numeric_limits<float>::quiet_NaN(), 0.5f, 1.f, 1.f),
                             glm::vec4(std::numeric_limits<float>::infinity(), 0.5f, 1.f, 1.f)};
    for (const auto& sample : samples)
    {
        const Texture source(device,
                             {.Format = Format::RGBA32Float,
                              .Extent = {1u, 1u, 1u},
                              .Usage = TextureUsage::Sampled | TextureUsage::CopyDst,
                              .DebugName = "Degenerate normal"},
                             TextureViewDimension::Texture2D);
        context->UploadTexture(
            source.GetHandle(),
            TextureUpload{.Data = std::as_bytes(std::span(&sample, 1)), .Extent = {1u, 1u, 1u}});
        context->Flush();
        CheckPixel(DecodeNormals(device, source), 0, {0.5f, 0.5f, 1.f, 1.f});
    }
}
