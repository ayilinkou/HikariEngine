#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include <asset/KtxReader.h>
#include <rhi/BarrierPresets.h>
#include <rhi/BindGroup.h>
#include <rhi/IDevice.h>
#include <rhi/Pipeline.h>
#include <rhi/UniqueHandle.h>

#include "GpuReadback.h"
#include "KtxFixture.h"
#include "RhiTestFixture.h"
#include "TextureCookFixture.h"
#include "ValidationGuard.h"

using namespace Hikari::Rhi;

namespace
{
std::vector<TextureUpload> Uploads(const Hikari::Asset::TextureData& data)
{
    std::vector<TextureUpload> uploads;
    for (const auto& resource : data.Subresources)
        uploads.push_back({.Data = std::span(data.Data).subspan(resource.Offset, resource.Size),
                           .MipLevel = resource.MipLevel,
                           .BaseLayer = resource.Face,
                           .Extent = resource.Extent});
    // File order, CPU storage order and submission order need not agree.
    std::reverse(uploads.begin(), uploads.end());
    return uploads;
}

constexpr std::array<uint16_t, 8> kColors{0xF800, 0x07E0, 0x001F, 0xFFE0,
                                          0xF81F, 0x07FF, 0xFFFF, 0x0000};

void FillBc1(uint32_t mip, uint32_t face, std::vector<std::byte>& payload, bool cube)
{
    const uint32_t width = std::max(12u >> mip, 1u);
    const uint32_t rows = (width + 3) / 4;
    for (uint32_t y = 0; y < rows; ++y)
        for (uint32_t x = 0; x < rows; ++x)
        {
            const uint32_t quadrant =
                rows == 1 ? 0u : (x * 4 >= width / 2 ? 1u : 0u) + (y * 4 >= width / 2 ? 2u : 0u);
            const uint16_t color = kColors[(mip + (cube ? face : quadrant)) % kColors.size()];
            const size_t offset = (y * rows + x) * 8u;
            payload[offset] = static_cast<std::byte>(color & 255u);
            payload[offset + 1] = static_cast<std::byte>(color >> 8u);
            // Select endpoint 0, including when its value is black. With
            // c0 == c1, selector 0 is still opaque in BC1's three-colour mode.
            payload[offset + 2] = payload[offset];
            payload[offset + 3] = payload[offset + 1];
            std::fill_n(payload.begin() + offset + 4, 4, std::byte{0});
        }
}

UniqueHandle<ShaderModuleHandle> LoadShader(IDevice& device, bool cube)
{
    const auto filename = std::string(cube ? "KtxSampleCube.comp." : "KtxSample.comp.") +
                          std::string(device.GetCaps().ShaderExtension);
    const auto path = std::filesystem::path(HIKARI_KTX_SHADER_DIR) / filename;
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.is_open());
    const std::vector<char> code((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
    return {device, device.CreateShaderModule(
                        {.Bytes = std::as_bytes(std::span(code)), .DebugName = filename})};
}
} // namespace

TEST_CASE("Loaded KTX2 native blocks upload every mip and face through the RHI",
          "[rhi][gpu][upload][ktx]")
{
    TextureCookFixture files;
    const auto path = files.Root() / "upload.ktx2";
    for (const auto config : RhiTest::AllDeviceConfigs())
    {
        INFO(RhiTest::Describe(config));
        IDevice& device = RhiTest::RequireDevice(config);
        const RhiTest::ValidationGuard guard(device);
        uint32_t tested = 0;
        for (const uint32_t native :
             {133u, 134u, 135u, 136u, 137u, 138u, 139u, 140u, 141u, 142u, 145u, 146u})
            for (const uint32_t faces : {1u, 6u})
            {
                CAPTURE(native, faces);
                const auto fixture = MakeKtxFixture(native, 12, 12, 4, faces);
                TextureCookFixture::WriteText(path, fixture.Bytes);
                std::string error;
                const auto data = Hikari::Asset::ReadKtxTexture(path, error);
                INFO(error);
                REQUIRE(data);
                constexpr TextureUsage kUsage =
                    TextureUsage::Sampled | TextureUsage::CopyDst | TextureUsage::CopySrc;
                if (!device.IsFormatSupported(data->Format, kUsage))
                    continue;
                ++tested;
                const UniqueHandle<TextureHandle> texture(
                    device,
                    device.CreateTexture({.Format = data->Format,
                                          .Extent = {data->Extent.Width, data->Extent.Height, 1u},
                                          .MipLevels = data->MipLevels,
                                          .ArrayLayers = data->Faces,
                                          .Usage = kUsage,
                                          .bCubeCompatible = faces == 6,
                                          .DebugName = "Loaded KTX2 blocks"}));
                const auto context =
                    device.CreateUploadContext({.StagingBudget = 1u, .DebugName = "KTX2 upload"});
                const auto uploads = Uploads(*data);
                context->UploadTexture(texture.Get(), uploads);
                context->Flush();
                CHECK(context->GetStats().Uploads == 1u);
                CHECK(context->GetStats().Batches == 1u);
                for (uint32_t mip = 0; mip < data->MipLevels; ++mip)
                {
                    const auto layers = RhiTest::ReadTextureLayers(device, texture.Get(), mip);
                    for (uint32_t face = 0; face < faces; ++face)
                        CHECK(layers[face] == fixture.Payloads[mip * faces + face]);
                }
            }
        if (tested == 0)
            SKIP("Device has no supported native BC formats");
    }
}

TEST_CASE("Loaded KTX2 2D and cube views sample known mips through both backends",
          "[rhi][gpu][rendering][ktx]")
{
    TextureCookFixture files;
    const auto path = files.Root() / "sample.ktx2";
    for (const auto config : RhiTest::AllDeviceConfigs())
    {
        INFO(RhiTest::Describe(config));
        IDevice& device = RhiTest::RequireDevice(config);
        const RhiTest::ValidationGuard guard(device);
        for (const bool cube : {false, true})
        {
            CAPTURE(cube);
            const auto fixture =
                MakeKtxFixture(133, 12, 12, 4, cube ? 6 : 1, {{"KTXorientation", "rd"}},
                               [cube](uint32_t mip, uint32_t face, auto& payload)
                               { FillBc1(mip, face, payload, cube); });
            TextureCookFixture::WriteText(path, fixture.Bytes);
            std::string error;
            const auto data = Hikari::Asset::ReadKtxTexture(path, error);
            INFO(error);
            REQUIRE(data);
            if (!device.IsFormatSupported(data->Format,
                                          TextureUsage::Sampled | TextureUsage::CopyDst))
                SKIP("Device has no BC1 sampling support");
            const UniqueHandle<TextureHandle> texture(
                device,
                device.CreateTexture({.Format = data->Format,
                                      .Extent = {12u, 12u, 1u},
                                      .MipLevels = 4u,
                                      .ArrayLayers = data->Faces,
                                      .Usage = TextureUsage::Sampled | TextureUsage::CopyDst,
                                      .bCubeCompatible = cube,
                                      .DebugName = "KTX2 sampled texture"}));
            const UniqueHandle<TextureViewHandle> view(
                device,
                device.CreateTextureView({.Texture = texture.Get(),
                                          .Dimension = cube ? TextureViewDimension::TextureCube
                                                            : TextureViewDimension::Texture2D,
                                          .MipCount = 4u,
                                          .LayerCount = data->Faces,
                                          .DebugName = "KTX2 full mip view"}));
            const auto context = device.CreateUploadContext({});
            const auto uploads = Uploads(*data);
            context->UploadTexture(texture.Get(), uploads);
            context->Flush();
            const uint32_t rows = cube ? 6u : 4u;
            const UniqueHandle<TextureHandle> result(
                device,
                device.CreateTexture(
                    {.Format = Format::RGBA8Unorm,
                     .Extent = {4u, rows, 1u},
                     .Usage = TextureUsage::Storage | TextureUsage::Sampled | TextureUsage::CopySrc,
                     .DebugName = "KTX2 sampling result"}));
            const UniqueHandle<TextureViewHandle> resultView(
                device, device.CreateTextureView(
                            {.Texture = result.Get(), .DebugName = "KTX2 result view"}));
            const UniqueHandle<SamplerHandle> sampler(
                device, device.CreateSampler({.MagFilter = Filter::Nearest,
                                              .MinFilter = Filter::Nearest,
                                              .MipmapFilter = MipmapMode::Nearest,
                                              .MaxLod = 3.f,
                                              .DebugName = "KTX2 mip sampler"}));
            const std::array bindings{
                BindGroupLayoutBinding{
                    .Slot = 0u, .Type = BindingType::Texture, .Visibility = ShaderStage::Compute},
                BindGroupLayoutBinding{
                    .Slot = 1u, .Type = BindingType::Sampler, .Visibility = ShaderStage::Compute},
                BindGroupLayoutBinding{.Slot = 2u,
                                       .Type = BindingType::UnorderedAccessTexture,
                                       .Visibility = ShaderStage::Compute}};
            const UniqueHandle<BindGroupLayoutHandle> groupLayout(
                device, device.CreateBindGroupLayout(
                            {.Bindings = bindings, .DebugName = "KTX2 sampling bindings"}));
            const std::array contents{
                BindGroupBinding{.Slot = 0u, .Type = BindingType::Texture, .View = view.Get()},
                BindGroupBinding{
                    .Slot = 1u, .Type = BindingType::Sampler, .Sampler = sampler.Get()},
                BindGroupBinding{.Slot = 2u,
                                 .Type = BindingType::UnorderedAccessTexture,
                                 .View = resultView.Get()}};
            const UniqueHandle<BindGroupHandle> group(
                device, device.CreateBindGroup({.Layout = groupLayout.Get(),
                                                .Bindings = contents,
                                                .DebugName = "KTX2 sample resources"}));
            const std::array layouts{groupLayout.Get()};
            const UniqueHandle<PipelineLayoutHandle> layout(
                device, device.CreatePipelineLayout(
                            {.BindGroupLayouts = layouts, .DebugName = "KTX2 sample layout"}));
            const auto shader = LoadShader(device, cube);
            const auto cache =
                device.CreatePipelineCache({.Path = {}, .DebugName = "KTX2 sample cache"});
            const UniqueHandle<ComputePipelineHandle> pipeline(
                device, device.CreateComputePipeline({.Layout = layout.Get(),
                                                      .Shader = {.Module = shader.Get()},
                                                      .DebugName = "KTX2 sampler"},
                                                     *cache));
            RhiTest::RunGraphicsCommands(
                device,
                [&](ICommandList& list)
                {
                    list.Barrier(BarrierPresets::UndefinedToUnorderedAccess().On(result.Get()));
                    list.SetPipeline(pipeline.Get());
                    list.SetComputeBindGroup(layout.Get(), 0u, group.Get());
                    list.Dispatch(4u, rows, 1u);
                    list.Barrier(
                        BarrierPresets::UnorderedAccessToShaderResource(PipelineStage::ComputeStage)
                            .On(result.Get()));
                });
            const auto pixels = RhiTest::ReadTextureLayers(device, result.Get());
            for (uint32_t row = 0; row < rows; ++row)
                for (uint32_t mip = 0; mip < 4; ++mip)
                {
                    // For 3x3 and 1x1 the single block has one colour. Larger
                    // levels reveal orientation with distinct quadrant blocks.
                    const uint32_t quadrant = cube ? row : mip >= 2 ? 0u : row;
                    const uint16_t color = kColors[(mip + quadrant) % kColors.size()];
                    const std::array<uint8_t, 4> expected{
                        static_cast<uint8_t>(color & 0xF800 ? 255 : 0),
                        static_cast<uint8_t>(color & 0x07E0 ? 255 : 0),
                        static_cast<uint8_t>(color & 0x001F ? 255 : 0), 255};
                    for (size_t channel = 0; channel < expected.size(); ++channel)
                    {
                        CAPTURE(row, mip, channel);
                        CHECK(pixels[0][(row * 4 + mip) * 4 + channel] ==
                              static_cast<std::byte>(expected[channel]));
                    }
                }
        }
    }
}
