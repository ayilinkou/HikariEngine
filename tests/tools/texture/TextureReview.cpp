#include "TextureReview.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <asset/ImageWriter.h>
#include <rhi/BarrierPresets.h>
#include <rhi/BufferDesc.h>
#include <rhi/ICommandAllocator.h>
#include <rhi/ICommandList.h>
#include <rhi/Pipeline.h>
#include <rhi/Submit.h>
#include <rhi/UniqueHandle.h>

#include "Texture.h"
#include "TextureSampling.h"
#include "Vertex.h"

using namespace Hikari;
using namespace Hikari::Rhi;

namespace
{
struct ReviewConstants
{
    uint32_t Face;
    uint32_t NormalEncoding;
    uint32_t IsNormal;
    uint32_t IsColor;
};

void SubmitAndWait(IDevice& device, const std::function<void(ICommandList&)>& record)
{
    const auto allocator = device.CreateCommandAllocator(
        {.Queue = QueueType::Graphics, .DebugName = "Texture review"});
    auto& list = allocator->Acquire();
    list.Begin();
    record(list);
    list.End();
    const UniqueHandle<FenceHandle> fence(device, device.CreateFence({}));
    ICommandList* command = &list;
    const FenceOperation done{.Fence = fence.Get(), .Value = 1};
    device.Submit(
        {.Queue = QueueType::Graphics, .CommandLists = {&command, 1}, .SignalFences = {&done, 1}});
    device.WaitForFence(fence.Get(), 1);
}

UniqueHandle<ShaderModuleHandle> Shader(IDevice& device, const std::string& name)
{
    const auto filename = name + "." + device.GetCaps().ShaderExtension;
    std::ifstream input(std::filesystem::path(HIKARI_REVIEW_SHADER_DIR) / filename,
                        std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot open review shader " + filename);
    const std::vector<char> bytes((std::istreambuf_iterator<char>(input)),
                                  std::istreambuf_iterator<char>());
    return {device, device.CreateShaderModule(
                        {.Bytes = std::as_bytes(std::span(bytes)), .DebugName = filename})};
}
} // namespace

nlohmann::json RenderTextureReview(IDevice& device, const Asset::TextureData& data,
                                   TextureViewHandle source, const std::filesystem::path& directory)
{
    std::filesystem::create_directories(directory);
    const UniqueHandle<SamplerHandle> sampler(device,
                                              device.CreateSampler(Engine::SurfaceSamplerDesc()));
    const std::array bindings{
        BindGroupLayoutBinding{
            .Slot = 0, .Type = BindingType::Texture, .Visibility = ShaderStage::Pixel},
        BindGroupLayoutBinding{
            .Slot = 1, .Type = BindingType::Sampler, .Visibility = ShaderStage::Pixel}};
    const UniqueHandle<BindGroupLayoutHandle> groupLayout(
        device,
        device.CreateBindGroupLayout({.Bindings = bindings, .DebugName = "Texture review"}));
    const std::array contents{
        BindGroupBinding{.Slot = 0, .Type = BindingType::Texture, .View = source},
        BindGroupBinding{.Slot = 1, .Type = BindingType::Sampler, .Sampler = sampler.Get()}};
    const UniqueHandle<BindGroupHandle> group(
        device,
        device.CreateBindGroup(
            {.Layout = groupLayout.Get(), .Bindings = contents, .DebugName = "Texture review"}));
    const std::array layouts{groupLayout.Get()};
    const std::array push{
        PushConstantRange{.Stages = ShaderStage::Pixel, .Size = sizeof(ReviewConstants)}};
    const UniqueHandle<PipelineLayoutHandle> layout(
        device, device.CreatePipelineLayout({.BindGroupLayouts = layouts,
                                             .PushConstantRanges = push,
                                             .DebugName = "Texture review"}));
    const std::string name = data.Faces == 6 ? "TextureReviewCube" : "TextureReview";
    const auto vertex = Shader(device, name + ".vert");
    const auto pixel = Shader(device, name + ".frag");
    const auto cache = device.CreatePipelineCache({.Path = {}, .DebugName = "Texture review"});
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
                                               .DebugName = "Texture review"},
                                              *cache));
    // Match the framebuffer's top edge on both backends without flipping texture UVs.
    const float top = device.GetCaps().bFlipClipSpaceY ? -1.f : 1.f;
    const std::array vertices{QuadVertex{{-1.f, top}, {0.f, 0.f}},
                              QuadVertex{{3.f, top}, {2.f, 0.f}},
                              QuadVertex{{-1.f, -3.f * top}, {0.f, 2.f}}};
    constexpr std::array<uint32_t, 3> indices{0, 1, 2};
    const UniqueHandle<BufferHandle> vertexBuffer(
        device, device.CreateBuffer({.Size = sizeof(vertices),
                                     .Usage = BufferUsage::Vertex | BufferUsage::CopyDst,
                                     .DebugName = "Texture review vertices"}));
    const UniqueHandle<BufferHandle> indexBuffer(
        device, device.CreateBuffer({.Size = sizeof(indices),
                                     .Usage = BufferUsage::Index | BufferUsage::CopyDst,
                                     .DebugName = "Texture review indices"}));
    const auto uploads = device.CreateUploadContext({});
    uploads->UploadBuffer(vertexBuffer.Get(), 0, std::as_bytes(std::span(vertices)));
    uploads->UploadBuffer(indexBuffer.Get(), 0, std::as_bytes(std::span(indices)));
    uploads->Flush();
    nlohmann::json images = nlohmann::json::array();
    for (uint32_t mip : {0u, 2u, 4u})
    {
        if (mip >= data.MipLevels)
            continue;
        const Core::Extent2D extent{std::max(data.Extent.Width >> mip, 1u),
                                    std::max(data.Extent.Height >> mip, 1u)};
        for (uint32_t face = 0; face < data.Faces; ++face)
        {
            const Texture target(device,
                                 {.Format = Format::RGBA8Unorm,
                                  .Extent = {extent.Width, extent.Height, 1},
                                  .Usage = TextureUsage::ColorAttachment | TextureUsage::CopySrc,
                                  .DebugName = "Texture review output"},
                                 TextureViewDimension::Texture2D);
            const uint64_t byteCount = uint64_t(extent.Width) * extent.Height * 4;
            const UniqueHandle<BufferHandle> readback(
                device, device.CreateBuffer({.Size = byteCount,
                                             .Usage = BufferUsage::CopyDst,
                                             .Access = MemoryAccess::GpuToCpu,
                                             .DebugName = "Texture review readback"}));
            const ReviewConstants constants{
                face, Engine::NormalShaderEncoding(data.Metadata, data.Format),
                data.Metadata.Usage == Asset::TextureUsage::Normal ? 1u : 0u,
                data.Metadata.Usage == Asset::TextureUsage::Color ? 1u : 0u};
            const std::array targets{RenderTarget{.View = target.GetView(), .Load = LoadOp::Clear}};
            SubmitAndWait(
                device,
                [&](ICommandList& list)
                {
                    list.Barrier(BarrierPresets::UndefinedToRenderTarget().On(target.GetHandle()));
                    list.BeginRendering(
                        {.RenderArea = {.Extent = extent}, .RenderTargets = targets});
                    list.SetViewport(
                        {.Width = float(extent.Width), .Height = float(extent.Height)});
                    list.SetScissor({.Extent = extent});
                    list.SetPipeline(pipeline.Get());
                    list.SetBindGroup(layout.Get(), 0, group.Get());
                    list.PushConstants(layout.Get(), ShaderStage::Pixel, 0,
                                       std::as_bytes(std::span(&constants, 1)));
                    list.SetVertexBuffer(0, vertexBuffer.Get());
                    list.SetIndexBuffer(indexBuffer.Get(), IndexFormat::Uint32);
                    list.DrawIndexed(3, 1, 0, 0, 0);
                    list.EndRendering();
                    list.Barrier(TextureBarrier{.Texture = target.GetHandle(),
                                                .SrcStage = PipelineStage::RenderTarget,
                                                .SrcAccess = AccessFlags::RenderTargetWrite,
                                                .DstStage = PipelineStage::Copy,
                                                .DstAccess = AccessFlags::CopySrc,
                                                .OldLayout = TextureLayout::RenderTarget,
                                                .NewLayout = TextureLayout::CopySrc});
                    list.CopyTextureToBuffer(target.GetHandle(), readback.Get(),
                                             {.Extent = {extent.Width, extent.Height, 1}});
                });
            const void* mapped = device.GetMappedData(readback.Get());
            if (!mapped)
                throw std::runtime_error("Texture review readback is not mapped");
            std::vector<uint8_t> pixels(static_cast<size_t>(byteCount));
            std::memcpy(pixels.data(), mapped, pixels.size());
            const auto filename =
                "face-" + std::to_string(face) + "-mip-" + std::to_string(mip) + ".png";
            if (!Asset::WritePng(pixels, extent, (directory / filename).string()))
                throw std::runtime_error("Cannot write rendered review " + filename);
            images.push_back({{"face", face},
                              {"mip", mip},
                              {"file", "rendered/" + filename},
                              {"width", extent.Width},
                              {"height", extent.Height}});
        }
    }
    return images;
}
