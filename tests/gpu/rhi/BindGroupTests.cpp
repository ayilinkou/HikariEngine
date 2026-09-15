#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

#include <rhi/BindGroup.h>
#include <rhi/BufferDesc.h>
#include <rhi/IDevice.h>
#include <rhi/Pipeline.h>
#include <rhi/SamplerDesc.h>
#include <rhi/TextureDesc.h>
#include <rhi/TextureViewDesc.h>

#include "RhiTestFixture.h"
#include "ValidationGuard.h"

/**
 * What creating a bind group promises before anything binds it: every kind of binding
 * the renderer declares is accepted, an optional slot may be left empty, the group and
 * its layout are counted and destroyed, and a pipeline layout can be built over it.
 */
using namespace Hikari::Rhi;

namespace
{
/** One of each binding kind, spread over the stages that read them, with an optional slot. */
constexpr std::array kBindings{
    BindGroupLayoutBinding{.Slot = 0u,
                           .Type = BindingType::UniformBuffer,
                           .Visibility = ShaderStage::Vertex | ShaderStage::Pixel,
                           .bOptional = false},
    BindGroupLayoutBinding{.Slot = 1u,
                           .Type = BindingType::Texture,
                           .Visibility = ShaderStage::Pixel,
                           .bOptional = false},
    BindGroupLayoutBinding{
        .Slot = 2u, .Type = BindingType::Texture, .Visibility = ShaderStage::Pixel, .bOptional = true},
    BindGroupLayoutBinding{.Slot = 3u,
                           .Type = BindingType::Sampler,
                           .Visibility = ShaderStage::Pixel,
                           .bOptional = false},
};
} // namespace

TEST_CASE("A bind group of every binding kind is created, counted and destroyed",
          "[rhi][gpu][bindgroups]")
{
    IDevice& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);

    const uint32_t layoutsBefore = device.GetLiveBindGroupLayoutCount();
    const uint32_t groupsBefore = device.GetLiveBindGroupCount();

    // Odd-sized, so a backend that rounds a constant buffer up has something to round.
    const BufferHandle uniforms = device.CreateBuffer(BufferDesc{.Size = 100u,
                                                                 .Usage = BufferUsage::Uniform,
                                                                 .Access = MemoryAccess::CpuToGpu,
                                                                 .DebugName = "Test Uniforms"});
    const TextureHandle texture = device.CreateTexture(TextureDesc{.Format = Format::RGBA8Unorm,
                                                                   .Extent = {4u, 4u, 1u},
                                                                   .Usage = TextureUsage::Sampled,
                                                                   .DebugName = "Test Sampled"});
    const TextureViewHandle view = device.CreateTextureView(
        TextureViewDesc{.Texture = texture, .Format = Format::RGBA8Unorm, .DebugName = "Test View"});
    const SamplerHandle sampler = device.CreateSampler(SamplerDesc{.DebugName = "Test Sampler"});

    const BindGroupLayoutHandle layout = device.CreateBindGroupLayout(
        BindGroupLayoutDesc{.Bindings = kBindings, .DebugName = "Test Layout"});

    // Slot 2 is optional and left out.
    const std::array bindings{
        BindGroupBinding{.Slot = 0u,
                         .Type = BindingType::UniformBuffer,
                         .Buffer = uniforms,
                         .View = {},
                         .Sampler = {}},
        BindGroupBinding{
            .Slot = 1u, .Type = BindingType::Texture, .Buffer = {}, .View = view, .Sampler = {}},
        BindGroupBinding{
            .Slot = 3u, .Type = BindingType::Sampler, .Buffer = {}, .View = {}, .Sampler = sampler},
    };
    const BindGroupHandle group = device.CreateBindGroup(
        BindGroupDesc{.Layout = layout, .Bindings = bindings, .DebugName = "Test Group"});

    CHECK(device.GetLiveBindGroupLayoutCount() == layoutsBefore + 1u);
    CHECK(device.GetLiveBindGroupCount() == groupsBefore + 1u);

    const std::array layouts{layout};
    const std::array pushRanges{
        PushConstantRange{.Stages = ShaderStage::Pixel, .Offset = 0u, .Size = 16u}};
    const PipelineLayoutHandle pipelineLayout = device.CreatePipelineLayout(PipelineLayoutDesc{
        .BindGroupLayouts = layouts, .PushConstantRanges = pushRanges, .DebugName = "Test Pipeline"});

    device.Destroy(pipelineLayout);
    device.Destroy(group);
    device.Destroy(layout);
    device.Destroy(sampler);
    device.Destroy(view);
    device.Destroy(texture);
    device.Destroy(uniforms);

    CHECK(device.GetLiveBindGroupLayoutCount() == layoutsBefore);
    CHECK(device.GetLiveBindGroupCount() == groupsBefore);
}
