#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "vulkan/vulkan_raii.hpp"

#include <rhi/BarrierPresets.h>
#include <rhi/ICommandList.h>
#include <rhi/IDevice.h>
#include <rhi/IPresentTarget.h>
#include <rhi/RhiTypes.h>
#include <rhi/vulkan/VulkanNative.h>

#include "GpuReadback.h"
#include "RhiTestFixture.h"
#include "ValidationGuard.h"

// The headless half of the presentation seam.
//
// A device created without presentation support has no surface, so
// CreatePresentTarget hands back an OffscreenTarget instead of a swapchain.
// That is the only way these cases reach it — the target's type is deliberately
// not nameable from outside the module, which is also what makes these tests
// worth having: they exercise the offscreen path through exactly the interface
// the renderer uses, so anything they cover is covered for the renderer too.
//
// A swapchain cannot be tested here at all: it needs a surface, which needs a
// window, which a test binary does not have. What that costs is covered in the
// architecture plan's CI section, and is why the windowed path is still checked
// by running the application.
using namespace Rhi;

namespace
{
// The extent every case renders at. Non-square and not a power of two, so a
// row-pitch or a width/height transposition shows up as garbage rather than as
// a picture that happens to still be square.
constexpr Extent2D kExtent{253u, 101u};

// Clear colours whose components are all exactly 0 or 1, so the readback can
// assert exact bytes: any rounding an implementation applies converting a float
// clear value to UNORM8 lands on 0 or 255 either way. Distinct per frame and
// per channel, so both a stale frame and a swapped channel are visible.
constexpr std::array<std::array<float, 4>, 3> kFrameColors{
    std::array<float, 4>{1.f, 0.f, 0.f, 1.f},
    std::array<float, 4>{0.f, 1.f, 0.f, 1.f},
    std::array<float, 4>{0.f, 0.f, 1.f, 1.f},
};

// One frame's command pool and buffer. A pool each rather than one reset
// between frames, because the point of the loop below is to have every frame in
// flight at once: resetting a pool whose buffer the GPU is still reading is
// undefined behaviour, and it is exactly the overlap these cases exist to
// exercise.
struct FrameCommands
{
    vk::raii::CommandPool Pool = nullptr;
    vk::raii::CommandBuffer Buffer = nullptr;
};

FrameCommands MakeFrameCommands(IDevice& device)
{
    vk::raii::Device& vkDevice = Vulkan::GetDevice(device);

    FrameCommands frame;
    frame.Pool = vk::raii::CommandPool(
        vkDevice,
        vk::CommandPoolCreateInfo{.flags = vk::CommandPoolCreateFlagBits::eTransient,
                                  .queueFamilyIndex = Vulkan::GetGraphicsQueueFamily(device)});

    const vk::CommandBufferAllocateInfo allocInfo{.commandPool = *frame.Pool,
                                                  .level = vk::CommandBufferLevel::ePrimary,
                                                  .commandBufferCount = 1u};
    frame.Buffer = std::move(vk::raii::CommandBuffers(vkDevice, allocInfo).front());
    return frame;
}

// Clears `acquired` to `color` through a dynamic-rendering pass, which is the
// same shape as the renderer's composite pass: acquire, transition, render into
// the acquired view, transition to what comes next.
//
// It leaves the image in ShaderResource rather than Present. An offscreen image
// is not presentable and never can be — VK_IMAGE_LAYOUT_PRESENT_SRC_KHR belongs
// to VK_KHR_swapchain, which a device with no surface does not enable — so
// ShaderResource is the finished state that matches the target's Sampled usage,
// and it is what RhiTest::ReadTextureLayers expects to copy from.
void RecordClearFrame(IDevice& device, vk::CommandBuffer cmd, const AcquiredImage& acquired,
                      const std::array<float, 4>& color, Extent2D extent)
{
    const std::unique_ptr<ICommandList> list = Vulkan::WrapCommandList(device, cmd);
    list->Begin();
    list->Barrier(BarrierPresets::AcquiredImageToRenderTarget().On(acquired.Texture));

    const vk::ClearValue clearValue = vk::ClearColorValue(color[0], color[1], color[2], color[3]);
    const vk::RenderingAttachmentInfo colorAttachment{
        .imageView = Vulkan::GetImageView(device, acquired.View),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = clearValue};

    const vk::RenderingInfo renderingInfo{
        .renderArea = {.offset = {0, 0}, .extent = vk::Extent2D{extent.Width, extent.Height}},
        .layerCount = 1u,
        .colorAttachmentCount = 1u,
        .pColorAttachments = &colorAttachment};

    cmd.beginRendering(renderingInfo);
    cmd.endRendering();

    list->Barrier(BarrierPresets::RenderTargetToShaderResource().On(acquired.Texture));
    list->End();
}

// Submits `cmd` with the waits the acquire asked for and the signal the target
// requires before Present will accept the image.
//
// Vulkan-side because submitting is: the RHI hands out a command list and the
// semaphores, but not a queue, so this is the one part of a frame that a
// neutral test cannot express. It goes away with the escape hatch when
// submission moves behind the RHI in Stage 8.
void SubmitFrame(IDevice& device, vk::CommandBuffer cmd, std::span<const SemaphoreHandle> waitOn,
                 SemaphoreHandle signalOnComplete)
{
    std::vector<vk::Semaphore> waitSemaphores;
    std::vector<vk::PipelineStageFlags> waitStages;
    for (const SemaphoreHandle handle : waitOn)
    {
        waitSemaphores.push_back(Vulkan::GetSemaphore(device, handle));
        waitStages.push_back(vk::PipelineStageFlagBits::eColorAttachmentOutput);
    }

    const vk::Semaphore signalSemaphore = Vulkan::GetSemaphore(device, signalOnComplete);
    const vk::SubmitInfo submitInfo{.waitSemaphoreCount =
                                        static_cast<uint32_t>(waitSemaphores.size()),
                                    .pWaitSemaphores = waitSemaphores.data(),
                                    .pWaitDstStageMask = waitStages.data(),
                                    .commandBufferCount = 1u,
                                    .pCommandBuffers = &cmd,
                                    .signalSemaphoreCount = 1u,
                                    .pSignalSemaphores = &signalSemaphore};

    Vulkan::GetGraphicsQueue(device).submit(submitInfo, nullptr);
}

// The bytes a clear to `color` leaves in memory, in `format`'s channel order.
// Written out rather than assumed, because getting it wrong is precisely the
// mistake a readback is meant to catch — the renderer's screenshot writer has a
// hardcoded BGRA swizzle for exactly this reason.
std::array<std::byte, 4> ExpectedTexel(Format format, const std::array<float, 4>& color)
{
    const auto quantize = [](float value)
    { return static_cast<std::byte>(static_cast<unsigned char>(value * 255.f)); };

    if (format == Format::BGRA8Unorm)
        return {quantize(color[2]), quantize(color[1]), quantize(color[0]), quantize(color[3])};

    REQUIRE(format == Format::RGBA8Unorm);
    return {quantize(color[0]), quantize(color[1]), quantize(color[2]), quantize(color[3])};
}
} // namespace

TEST_CASE("A device with no surface hands back an offscreen present target", "[rhi][gpu][present]")
{
    IDevice& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);

    REQUIRE_FALSE(device.GetCaps().bPresentSupported);

    const std::unique_ptr<IPresentTarget> target =
        device.CreatePresentTarget(PresentTargetDesc{.Extent = kExtent, .FramesInFlight = 2u});
    REQUIRE(target != nullptr);

    // The extent is honoured exactly. A swapchain's is clamped to what the
    // surface allows; nothing clamps this one, which is what makes a headless
    // capture reproducible across machines.
    CHECK(target->GetExtent() == kExtent);
    CHECK(target->GetImageCount() == 2u);

    // Undefined would mean the target had no format it could name, which the
    // renderer would then hand to pipeline creation.
    CHECK(target->GetFormat() != Format::Undefined);
}

TEST_CASE("An offscreen acquire always succeeds and cycles its images", "[rhi][gpu][present]")
{
    IDevice& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);

    const std::unique_ptr<IPresentTarget> target =
        device.CreatePresentTarget(PresentTargetDesc{.Extent = kExtent, .FramesInFlight = 3u});

    REQUIRE(target->GetImageCount() == 3u);

    // Never asks to be recreated: there is no surface to go out of date. The
    // renderer's out-of-date branch is therefore dead in a headless run, rather
    // than something it has to be able to do without a window.
    for (uint32_t frame = 0u; frame < 7u; frame++)
    {
        const AcquiredImage acquired = target->Acquire();
        CHECK_FALSE(acquired.bNeedsRecreate);
        CHECK(acquired.Index == frame % 3u);
        CHECK(acquired.Texture.IsValid());
        CHECK(acquired.View.IsValid());

        // Nothing has been submitted, so no image has a render-complete signal
        // outstanding and there is nothing to wait on — including on the second
        // pass over the images.
        CHECK(acquired.WaitSemaphores.empty());
    }
}

// The step's headline check: three frames through the same acquire / render /
// present sequence the windowed renderer runs, into a target with no window,
// with the frames overlapping rather than being waited on one at a time.
//
// Two images and three frames is the smallest arrangement that reuses one, so
// frame 2 has to wait on the render-complete semaphore frame 0 signalled. That
// is both the real write-after-write dependency and the only thing that leaves
// the semaphore unsignalled in time for frame 2 to signal it again — a target
// that dropped it would fail here with a validation error rather than by
// rendering something subtly wrong.
TEST_CASE("Three overlapping frames render into an offscreen target", "[rhi][gpu][present]")
{
    IDevice& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);

    const std::unique_ptr<IPresentTarget> target =
        device.CreatePresentTarget(PresentTargetDesc{.Extent = kExtent, .FramesInFlight = 2u});
    REQUIRE(target->GetImageCount() == 2u);

    std::vector<FrameCommands> frames;
    frames.reserve(kFrameColors.size());
    for (size_t i = 0; i < kFrameColors.size(); i++)
        frames.push_back(MakeFrameCommands(device));

    std::array<TextureHandle, 2> imagesByIndex{};

    for (size_t frame = 0; frame < kFrameColors.size(); frame++)
    {
        const AcquiredImage acquired = target->Acquire();
        REQUIRE_FALSE(acquired.bNeedsRecreate);

        // Only the third frame reuses an image, and only it has a previous
        // write to wait for.
        CHECK(acquired.WaitSemaphores.size() == (frame < 2u ? 0u : 1u));

        imagesByIndex[acquired.Index] = acquired.Texture;

        RecordClearFrame(device, *frames[frame].Buffer, acquired, kFrameColors[frame], kExtent);
        SubmitFrame(device, *frames[frame].Buffer, acquired.WaitSemaphores,
                    target->GetRenderCompleteSemaphore(acquired.Index));

        CHECK(target->Present(acquired.Index));
    }

    device.WaitIdle();

    // Image 0 was written by frames 0 and 2, image 1 by frame 1, so what
    // survives is the last colour each of them was cleared to. Checking both
    // is what catches an Acquire that hands back the same image every time.
    const std::array<size_t, 2> lastFrameForImage{2u, 1u};
    for (uint32_t index = 0u; index < 2u; index++)
    {
        INFO("image index " << index);

        const std::array<std::byte, 4> expected =
            ExpectedTexel(target->GetFormat(), kFrameColors[lastFrameForImage[index]]);

        const std::vector<std::vector<std::byte>> layers =
            RhiTest::ReadTextureLayers(device, imagesByIndex[index]);
        REQUIRE(layers.size() == 1u);

        const std::vector<std::byte>& pixels = layers.front();
        REQUIRE(pixels.size() == static_cast<size_t>(kExtent.Width) * kExtent.Height * 4u);

        // Every texel, not a sample of them: a copy that got the row pitch
        // wrong still produces the right bytes at the origin.
        size_t mismatches = 0u;
        for (size_t texel = 0; texel < pixels.size() / 4u; texel++)
        {
            for (size_t channel = 0; channel < 4u; channel++)
            {
                if (pixels[texel * 4u + channel] != expected[channel])
                    mismatches++;
            }
        }
        CHECK(mismatches == 0u);
    }
}

TEST_CASE("Recreating an offscreen target resizes it", "[rhi][gpu][present]")
{
    IDevice& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);

    const std::unique_ptr<IPresentTarget> target =
        device.CreatePresentTarget(PresentTargetDesc{.Extent = kExtent, .FramesInFlight = 2u});

    // A frame first, so the recreate below has a signalled render-complete
    // semaphore and a live image to tear down rather than a pristine target.
    {
        const FrameCommands frame = MakeFrameCommands(device);
        const AcquiredImage acquired = target->Acquire();
        RecordClearFrame(device, *frame.Buffer, acquired, kFrameColors[0], kExtent);
        SubmitFrame(device, *frame.Buffer, acquired.WaitSemaphores,
                    target->GetRenderCompleteSemaphore(acquired.Index));
        CHECK(target->Present(acquired.Index));
        device.WaitIdle();
    }

    constexpr Extent2D kSmaller{64u, 200u};
    REQUIRE(target->Recreate(kSmaller));
    CHECK(target->GetExtent() == kSmaller);
    CHECK(target->GetImageCount() == 2u);

    // Rebuilt from scratch, so the first pass over the new images has nothing
    // outstanding to wait on even though the old ones did.
    const AcquiredImage acquired = target->Acquire();
    CHECK(acquired.WaitSemaphores.empty());

    const FrameCommands frame = MakeFrameCommands(device);
    RecordClearFrame(device, *frame.Buffer, acquired, kFrameColors[1], kSmaller);
    SubmitFrame(device, *frame.Buffer, acquired.WaitSemaphores,
                target->GetRenderCompleteSemaphore(acquired.Index));
    CHECK(target->Present(acquired.Index));

    device.WaitIdle();
}

// A zero extent is the one request that cannot be met, and the answer is the
// same "nothing was touched, ask again" a minimised window gets from a
// swapchain — so a caller that resizes through zero needs no special case for
// which kind of target it holds.
TEST_CASE("Recreating an offscreen target at a zero extent changes nothing", "[rhi][gpu][present]")
{
    IDevice& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);

    const std::unique_ptr<IPresentTarget> target =
        device.CreatePresentTarget(PresentTargetDesc{.Extent = kExtent, .FramesInFlight = 2u});

    CHECK_FALSE(target->Recreate(Extent2D{0u, 0u}));
    CHECK_FALSE(target->Recreate(Extent2D{kExtent.Width, 0u}));
    CHECK_FALSE(target->Recreate(Extent2D{0u, kExtent.Height}));

    CHECK(target->GetExtent() == kExtent);

    // Still usable: the images the failed recreates left alone are the ones
    // that were already there.
    const AcquiredImage acquired = target->Acquire();
    CHECK_FALSE(acquired.bNeedsRecreate);
    CHECK(acquired.Texture.IsValid());
}

// The target owns its images, unlike a swapchain's, so destroying it has to
// give every one of them back. A leak here would be invisible in a windowed run
// and would grow with every resize in a headless one.
TEST_CASE("An offscreen target frees its images when it is destroyed", "[rhi][gpu][present]")
{
    IDevice& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);

    const uint32_t texturesBefore = device.GetLiveTextureCount();
    const uint32_t viewsBefore = device.GetLiveTextureViewCount();

    {
        const std::unique_ptr<IPresentTarget> target =
            device.CreatePresentTarget(PresentTargetDesc{.Extent = kExtent, .FramesInFlight = 3u});

        CHECK(device.GetLiveTextureCount() == texturesBefore + 3u);
        CHECK(device.GetLiveTextureViewCount() == viewsBefore + 3u);

        REQUIRE(target->Recreate(Extent2D{128u, 128u}));

        // A recreate replaces them rather than adding to them, which is the
        // half of a resize that a fixed count cannot tell you about.
        CHECK(device.GetLiveTextureCount() == texturesBefore + 3u);
        CHECK(device.GetLiveTextureViewCount() == viewsBefore + 3u);
    }

    CHECK(device.GetLiveTextureCount() == texturesBefore);
    CHECK(device.GetLiveTextureViewCount() == viewsBefore);
}
