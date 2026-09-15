#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <rhi/BindGroup.h>
#include <rhi/IDevice.h>
#include <rhi/Pipeline.h>
#include <rhi/PipelineCache.h>
#include <rhi/UniqueHandle.h>

#include "BindGroupLayouts.h"
#include "InstanceData.h"
#include "RhiTestFixture.h"
#include "ValidationGuard.h"
#include "Vertex.h"
#include "shaders/ShaderTypes.h"

/**
 * The renderer's pipelines, created from the shaders the build compiled, before
 * anything can draw with them.
 *
 * A pipeline is where every description the engine hands the device meets the
 * compiled shader it describes: the bind group layouts against the shader's
 * registers, the vertex tables against its inputs, the formats and blends
 * against its outputs. Both APIs check that agreement when the pipeline is
 * created, so creating each one under the debug layer or the validation layer
 * is what proves a backend accepts them.
 *
 * The descriptions are copies of the renderer's, which builds them inline where
 * it cannot be reached from a test. The tables they are built from — the
 * layouts, the vertex and instance streams, the constant blocks — are the
 * engine's own, and those are what a shader change moves.
 */
using namespace Hikari::Rhi;

namespace
{
/** A compiled stage as the device eats it, named as the engine names it. */
class ShaderLoader
{
public:
    explicit ShaderLoader(IDevice& device) : m_Device(device) {}

    ShaderStageDesc Load(const std::string& name)
    {
        const std::string file = std::format("{}.{}", name, m_Device.GetCaps().ShaderExtension);
        const std::filesystem::path path = std::filesystem::path(HIKARI_SHADER_DIR) / file;
        INFO("shader: " << path.string());

        std::ifstream stream(path, std::ios::binary);
        REQUIRE(stream.is_open());
        const std::vector<char> code((std::istreambuf_iterator<char>(stream)),
                                     std::istreambuf_iterator<char>());
        REQUIRE_FALSE(code.empty());

        m_Modules.emplace_back(
            m_Device, m_Device.CreateShaderModule(ShaderModuleDesc{
                          .Bytes = std::as_bytes(std::span(code)), .DebugName = file}));

        return ShaderStageDesc{.Module = m_Modules.back().Get()};
    }

private:
    IDevice& m_Device;
    std::vector<UniqueHandle<ShaderModuleHandle>> m_Modules;
};

UniqueHandle<BindGroupLayoutHandle> MakeLayout(IDevice& device,
                                               std::span<const BindGroupLayoutBinding> bindings,
                                               const char* name)
{
    return {device, device.CreateBindGroupLayout(
                        BindGroupLayoutDesc{.Bindings = bindings, .DebugName = name})};
}

UniqueHandle<PipelineLayoutHandle> MakePipelineLayout(
    IDevice& device, std::span<const BindGroupLayoutHandle> layouts,
    std::span<const PushConstantRange> pushRanges, const char* name)
{
    return {device, device.CreatePipelineLayout(PipelineLayoutDesc{
                        .BindGroupLayouts = layouts, .PushConstantRanges = pushRanges, .DebugName = name})};
}

/** The first depth format the device renders into, from the renderer's own ranking. */
Format FindDepthFormat(const IDevice& device)
{
    constexpr std::array candidates{Format::D32Float, Format::D32FloatS8Uint,
                                    Format::D24UnormS8Uint};
    for (const Format format : candidates)
    {
        if (device.IsFormatSupported(format, TextureUsage::DepthStencilAttachment))
            return format;
    }

    FAIL("no depth format the renderer ranks is supported");
    return Format::Undefined;
}
} // namespace

TEST_CASE("The renderer's graphics and compute pipelines are created from its shaders",
          "[rhi][gpu][pipelines]")
{
    IDevice& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);

    const std::unique_ptr<IPipelineCache> cache =
        device.CreatePipelineCache(PipelineCacheDesc{.Path = {}, .DebugName = "Test Cache"});
    ShaderLoader shaders(device);

    const auto global = MakeLayout(device, EngineBindGroups::kGlobal, "Global Layout");
    const auto material = MakeLayout(device, EngineBindGroups::kMaterial, "Material Layout");
    const auto composite = MakeLayout(device, EngineBindGroups::kComposite, "Composite Layout");
    const auto depth = MakeLayout(device, EngineBindGroups::kDepth, "Depth Layout");
    const auto cloudDispatch =
        MakeLayout(device, EngineBindGroups::kCloudDispatch, "Cloud Dispatch Layout");
    const auto cloudBake = MakeLayout(device, EngineBindGroups::kCloudBake, "Cloud Bake Layout");

    std::array<VertexAttribute, Vertex::AttributeCount + InstanceData::AttributeCount>
        surfaceAttributes{};
    const auto vertexAttributes = Vertex::GetAttributeDescriptions();
    const auto instanceAttributes = InstanceData::GetAttributeDescriptions();
    std::ranges::copy(vertexAttributes, surfaceAttributes.begin());
    std::ranges::copy(instanceAttributes, surfaceAttributes.begin() + Vertex::AttributeCount);

    const std::array surfaceBuffers{Vertex::GetBindingDescription(),
                                    InstanceData::GetBindingDescription()};
    const Format depthFormat = FindDepthFormat(device);

    const std::array surfaceLayouts{global.Get(), material.Get()};
    const std::array materialPush{PushConstantRange{
        .Stages = ShaderStage::Pixel, .Offset = 0u, .Size = sizeof(MaterialPushConstant)}};

    SECTION("Opaque, once per cull mode")
    {
        const auto layout =
            MakePipelineLayout(device, surfaceLayouts, materialPush, "Opaque Layout");
        std::vector<UniqueHandle<GraphicsPipelineHandle>> graphics;
        const std::array formats{Format::RGBA16Float};
        const std::array blends{RenderTargetBlend{}};
        const ShaderStageDesc vertex = shaders.Load("opaque.vert");
        const ShaderStageDesc pixel = shaders.Load("opaque.frag");

        for (const CullMode cull : {CullMode::Back, CullMode::None})
        {
            graphics.emplace_back(
                device,
                device.CreateGraphicsPipeline(
                    GraphicsPipelineDesc{
                        .Layout = layout.Get(),
                        .VertexShader = vertex,
                        .PixelShader = pixel,
                        .VertexBuffers = surfaceBuffers,
                        .VertexAttributes = surfaceAttributes,
                        .RenderTargetFormats = formats,
                        .RenderTargetBlends = blends,
                        .DepthFormat = depthFormat,
                        .Depth = {.bTest = true, .bWrite = true, .Compare = CompareOp::Less},
                        .Cull = cull,
                        .DebugName = "Opaque"},
                    *cache));
        }

        CHECK(graphics.size() == 2u);
    }

    SECTION("Weighted-blended transparency, into two targets")
    {
        const auto layout =
            MakePipelineLayout(device, surfaceLayouts, materialPush, "Transparent Layout");
        std::vector<UniqueHandle<GraphicsPipelineHandle>> graphics;
        const std::array formats{Format::RGBA16Float, Format::R8Unorm};
        const std::array blends{RenderTargetBlend{.bEnable = true,
                                                  .SrcColor = BlendFactor::One,
                                                  .DstColor = BlendFactor::One,
                                                  .ColorOp = BlendOp::Add,
                                                  .SrcAlpha = BlendFactor::One,
                                                  .DstAlpha = BlendFactor::One,
                                                  .AlphaOp = BlendOp::Add},
                                RenderTargetBlend{.bEnable = true,
                                                  .SrcColor = BlendFactor::Zero,
                                                  .DstColor = BlendFactor::OneMinusSrcColor,
                                                  .ColorOp = BlendOp::Add,
                                                  .SrcAlpha = BlendFactor::Zero,
                                                  .DstAlpha = BlendFactor::OneMinusSrcColor,
                                                  .AlphaOp = BlendOp::Add}};

        graphics.emplace_back(
            device, device.CreateGraphicsPipeline(
                        GraphicsPipelineDesc{
                            .Layout = layout.Get(),
                            .VertexShader = shaders.Load("weightedBlendedOIT.vert"),
                            .PixelShader = shaders.Load("weightedBlendedOIT.frag"),
                            .VertexBuffers = surfaceBuffers,
                            .VertexAttributes = surfaceAttributes,
                            .RenderTargetFormats = formats,
                            .RenderTargetBlends = blends,
                            .DepthFormat = depthFormat,
                            .Depth = {.bTest = true, .bWrite = false, .Compare = CompareOp::Less},
                            .Cull = CullMode::None,
                            .DebugName = "Transparent"},
                        *cache));

        CHECK(graphics.size() == 1u);
    }

    SECTION("Composite, into each format a present target may have")
    {
        const std::array layouts{global.Get(), composite.Get()};
        const auto layout = MakePipelineLayout(device, layouts, {}, "Composite Layout");
        std::vector<UniqueHandle<GraphicsPipelineHandle>> graphics;
        const std::array buffers{QuadVertex::GetBindingDescription()};
        const auto attributes = QuadVertex::GetAttributeDescription();
        const std::array blends{RenderTargetBlend{}};
        const ShaderStageDesc vertex = shaders.Load("composite.vert");
        const ShaderStageDesc pixel = shaders.Load("composite.frag");

        for (const Format format : {Format::BGRA8Unorm, Format::RGBA8Unorm})
        {
            const std::array formats{format};
            graphics.emplace_back(
                device,
                device.CreateGraphicsPipeline(GraphicsPipelineDesc{.Layout = layout.Get(),
                                                                   .VertexShader = vertex,
                                                                   .PixelShader = pixel,
                                                                   .VertexBuffers = buffers,
                                                                   .VertexAttributes = attributes,
                                                                   .RenderTargetFormats = formats,
                                                                   .RenderTargetBlends = blends,
                                                                   .DepthFormat = Format::Undefined,
                                                                   .Depth = {},
                                                                   .Cull = CullMode::None,
                                                                   .DebugName = "Composite"},
                                             *cache));
        }

        CHECK(graphics.size() == 2u);
    }

    SECTION("Clouds, reading depth")
    {
        const std::array layouts{global.Get(), depth.Get(), cloudDispatch.Get()};
        const std::array push{PushConstantRange{
            .Stages = ShaderStage::Compute, .Offset = 0u, .Size = sizeof(CloudPushConstants)}};
        const auto layout = MakePipelineLayout(device, layouts, push, "Clouds Layout");

        const UniqueHandle<ComputePipelineHandle> pipeline(
            device, device.CreateComputePipeline(
                        ComputePipelineDesc{.Layout = layout.Get(),
                                            .Shader = shaders.Load("clouds.comp"),
                                            .DebugName = "Clouds"},
                        *cache));

        CHECK(pipeline.Get().IsValid());
    }

    SECTION("The noise bake")
    {
        const std::array layouts{cloudBake.Get()};
        const std::array push{PushConstantRange{
            .Stages = ShaderStage::Compute, .Offset = 0u, .Size = sizeof(BakeConstants)}};
        const auto layout = MakePipelineLayout(device, layouts, push, "Bake Perlin Worley Layout");

        const UniqueHandle<ComputePipelineHandle> pipeline(
            device, device.CreateComputePipeline(
                        ComputePipelineDesc{.Layout = layout.Get(),
                                            .Shader = shaders.Load("bakePerlinWorley.comp"),
                                            .DebugName = "Bake Perlin Worley"},
                        *cache));

        CHECK(pipeline.Get().IsValid());
    }

    // Silent, not merely free of errors: a pipeline the layer warns about is one the
    // two backends may still disagree on.
    CHECK(device.GetDiagnostics().WarningCount() == 0u);
}
