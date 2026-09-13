#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <rhi/BarrierPresets.h>
#include <rhi/ICommandList.h>
#include <rhi/IDevice.h>
#include <rhi/Rendering.h>

#include "GpuReadback.h"
#include "RhiTestFixture.h"
#include "ValidationGuard.h"

/**
 * What a rendering scope does with nothing drawn in it: bind its targets and clear
 * them. The smallest thing a backend's rendering scope can be held to before it has
 * pipelines, and the thing every pass begins with.
 */
using namespace Hikari::Rhi;

TEST_CASE("A cleared rendering scope reads back as its clear colour", "[rhi][gpu][rendering]")
{
    IDevice& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);

    // Odd and not a power of two, so a clear restricted to the wrong area or a
    // readback packed at the wrong pitch cannot match by accident.
    constexpr uint32_t kWidth = 7u;
    constexpr uint32_t kHeight = 5u;

    const TextureHandle color = device.CreateTexture(TextureDesc{
        .Format = Format::RGBA8Unorm,
        .Extent = {kWidth, kHeight, 1u},
        .Usage = TextureUsage::ColorAttachment | TextureUsage::Sampled | TextureUsage::CopySrc,
        .DebugName = "Test Scope Color"});
    const TextureHandle depth =
        device.CreateTexture(TextureDesc{.Format = Format::D32Float,
                                         .Extent = {kWidth, kHeight, 1u},
                                         .Usage = TextureUsage::DepthStencilAttachment,
                                         .DebugName = "Test Scope Depth"});

    const TextureViewHandle colorView = device.CreateTextureView(
        TextureViewDesc{.Texture = color, .Format = Format::RGBA8Unorm, .DebugName = "Color View"});
    const TextureViewHandle depthView =
        device.CreateTextureView(TextureViewDesc{.Texture = depth,
                                                 .Format = Format::D32Float,
                                                 .Aspect = TextureAspect::Depth,
                                                 .DebugName = "Depth View"});

    // Every channel exactly representable in eight bits: 0.2 is 51/255.
    const RenderTarget target{.View = colorView,
                              .Load = LoadOp::Clear,
                              .Store = StoreOp::Preserve,
                              .ClearColor = {1.0f, 0.2f, 0.0f, 1.0f}};
    const DepthStencilTarget depthTarget{.View = depthView,
                                         .Load = LoadOp::Clear,
                                         .Store = StoreOp::Preserve,
                                         .ClearDepth = 1.0f,
                                         .ClearStencil = 0u,
                                         .bReadOnly = false};

    RhiTest::RunGraphicsCommands(
        device,
        [&](ICommandList& list)
        {
            list.Barrier(std::array{BarrierPresets::UndefinedToRenderTarget().On(color),
                                    BarrierPresets::UndefinedToDepthStencilWrite().On(depth)});

            list.BeginRendering(
                RenderingDesc{.RenderArea = {.Offset = {.X = 0, .Y = 0},
                                             .Extent = {.Width = kWidth, .Height = kHeight}},
                              .RenderTargets = std::span<const RenderTarget>(&target, 1),
                              .pDepthStencil = &depthTarget});
            list.SetViewport(Viewport{.X = 0.f,
                                      .Y = 0.f,
                                      .Width = static_cast<float>(kWidth),
                                      .Height = static_cast<float>(kHeight),
                                      .MinDepth = 0.f,
                                      .MaxDepth = 1.f});
            list.SetScissor(Rect2D{.Offset = {.X = 0, .Y = 0},
                                   .Extent = {.Width = kWidth, .Height = kHeight}});
            list.EndRendering();

            list.Barrier(BarrierPresets::RenderTargetToShaderResource().On(color));
        });

    const std::vector<std::vector<std::byte>> layers = RhiTest::ReadTextureLayers(device, color);
    REQUIRE(layers.size() == 1u);
    REQUIRE(layers[0].size() == kWidth * kHeight * 4u);

    const std::array<uint8_t, 4> expected{255u, 51u, 0u, 255u};
    for (size_t pixel = 0u; pixel < kWidth * kHeight; ++pixel)
    {
        for (size_t channel = 0u; channel < 4u; ++channel)
        {
            INFO("pixel " << pixel << ", channel " << channel);
            CHECK(static_cast<uint8_t>(layers[0][pixel * 4u + channel]) == expected[channel]);
        }
    }

    device.Destroy(colorView);
    device.Destroy(depthView);
    device.Destroy(color);
    device.Destroy(depth);
}
