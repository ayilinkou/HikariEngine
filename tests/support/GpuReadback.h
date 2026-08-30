#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "vulkan/vulkan_raii.hpp"

#include <rhi/Barrier.h>
#include <rhi/BufferDesc.h>
#include <rhi/Handles.h>
#include <rhi/ICommandList.h>
#include <rhi/IDevice.h>
#include <rhi/RhiTypes.h>
#include <rhi/TextureDesc.h>
#include <rhi/UniqueHandle.h>
#include <rhi/vulkan/VulkanNative.h>

/**
 * Getting the bytes back off the GPU, which is the only way a test can say what
 * an upload actually wrote.
 *
 * Vulkan-side because submitting is: the RHI hands out a command list (plan D7,
 * D8) but not a queue, so recording is neutral and the submission around it is
 * not. A second backend's tests would keep these functions' signatures and
 * rewrite their bodies.
 */
namespace RhiTest
{
/**
 * Records `record` into a command list of its own, submits it to the graphics
 * queue, and blocks until the GPU has finished with it.
 *
 * A pool per call rather than one shared across the binary: these run a handful
 * of times per test and the allocation is not what makes them slow, whereas a
 * shared pool would need resetting between uses and would make one test's
 * failure leave the next one recording into a half-used buffer.
 */
inline void RunGraphicsCommands(Hikari::Rhi::IDevice& device,
                                const std::function<void(Hikari::Rhi::ICommandList&)>& record,
                                std::optional<Hikari::Rhi::SemaphoreHandle> waitSemaphore = {})
{
    vk::raii::Device& vkDevice = Hikari::Rhi::Vulkan::GetDevice(device);

    const vk::CommandPoolCreateInfo poolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = Hikari::Rhi::Vulkan::GetGraphicsQueueFamily(device)};
    vk::raii::CommandPool pool(vkDevice, poolInfo);

    const vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = *pool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1u};
    vk::raii::CommandBuffer cmd = std::move(vk::raii::CommandBuffers(vkDevice, allocInfo).front());

    const std::unique_ptr<Hikari::Rhi::ICommandList> list = Hikari::Rhi::Vulkan::WrapCommandList(device, *cmd);
    list->Begin();
    record(*list);
    list->End();

    vk::raii::Fence fence(vkDevice, vk::FenceCreateInfo{});

    const vk::CommandBuffer rawCommandBuffer = *cmd;

    // Waiting rather than calling WaitIdle, when the caller has a semaphore to
    // give: the wait is the ordering, and a WaitIdle would hide a caller that
    // established none. ColorAttachmentOutput because that is what an offscreen
    // target's render-complete semaphore is signalled from.
    const vk::Semaphore waitHandle =
        waitSemaphore ? Hikari::Rhi::Vulkan::GetSemaphore(device, *waitSemaphore) : nullptr;
    constexpr vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;

    const vk::SubmitInfo submitInfo{.waitSemaphoreCount = waitSemaphore ? 1u : 0u,
                                    .pWaitSemaphores = waitSemaphore ? &waitHandle : nullptr,
                                    .pWaitDstStageMask = waitSemaphore ? &waitStage : nullptr,
                                    .commandBufferCount = 1u,
                                    .pCommandBuffers = &rawCommandBuffer};
    Hikari::Rhi::Vulkan::GetGraphicsQueue(device).submit(submitInfo, *fence);

    const vk::Result result =
        vkDevice.waitForFences(*fence, vk::True, std::numeric_limits<uint64_t>::max());
    REQUIRE(result == vk::Result::eSuccess);
}

/**
 * `size` bytes of `source`, copied into a readback buffer and out to the heap.
 *
 * `source` must carry BufferUsage::CopySrc. No barrier precedes the copy and
 * none is needed: whatever filled the buffer did so in an earlier submission
 * that a fence wait has already returned from, and a fence signal's access
 * scope is every access the device performed (Vulkan 1.4, *Fences*) — which is
 * the same guarantee IUploadContext::Flush relies on for the renderer.
 */
inline std::vector<std::byte> ReadBuffer(Hikari::Rhi::IDevice& device, Hikari::Rhi::BufferHandle source,
                                         uint64_t size)
{
    const Hikari::Rhi::UniqueHandle<Hikari::Rhi::BufferHandle> readback(
        device, device.CreateBuffer(Hikari::Rhi::BufferDesc{.Size = size,
                                                    .Usage = Hikari::Rhi::BufferUsage::CopyDst,
                                                    .Access = Hikari::Rhi::MemoryAccess::GpuToCpu,
                                                    .DebugName = "Readback"}));

    RunGraphicsCommands(device,
                        [&](Hikari::Rhi::ICommandList& list)
                        {
                            list.CopyBuffer(source, readback.Get(),
                                            Hikari::Rhi::BufferCopyRegion{.Size = size});
                        });

    const void* pMapped = device.GetMappedData(readback.Get());
    REQUIRE(pMapped != nullptr);

    std::vector<std::byte> bytes(static_cast<size_t>(size));
    std::memcpy(bytes.data(), pMapped, bytes.size());
    return bytes;
}

/**
 * Mip `mipLevel` of every array layer of `source`, one tightly packed entry per
 * layer.
 *
 * One call rather than one per layer because the layout transition has to cover
 * the whole texture: a layout is a property of a subresource, and transitioning
 * them one at a time would leave the rest where they were. Each layer is
 * nonetheless copied by its own region naming BaseLayer, which is what makes an
 * upload that wrote every face into layer 0 show up as five wrong layers rather
 * than as one buffer that happens to hold the right bytes somewhere.
 *
 * `source` must carry TextureUsage::CopySrc and be in the ShaderResource layout
 * — which is where IUploadContext leaves everything it fills. The texture is
 * left in CopySrc.
 */
inline std::vector<std::vector<std::byte>>
ReadTextureLayers(Hikari::Rhi::IDevice& device, Hikari::Rhi::TextureHandle source, uint32_t mipLevel = 0u)
{
    const Hikari::Rhi::TextureDesc* pDesc = device.GetTextureDesc(source);
    REQUIRE(pDesc != nullptr);

    const Hikari::Rhi::Extent3D extent{std::max(pDesc->Extent.Width >> mipLevel, 1u),
                               std::max(pDesc->Extent.Height >> mipLevel, 1u),
                               std::max(pDesc->Extent.Depth >> mipLevel, 1u)};

    const uint32_t layerCount = pDesc->ArrayLayers;

    // Zero means the format has no single texel size, which for a combined
    // depth/stencil format is the truth rather than a failure — this helper
    // copies one aspect and cannot pick. No test needs that today, so it is a
    // hard stop rather than an extra parameter nothing would pass.
    const uint32_t bytesPerTexel = Hikari::Rhi::BytesPerTexel(pDesc->Format);
    REQUIRE(bytesPerTexel != 0u);

    const uint64_t layerSize =
        static_cast<uint64_t>(extent.Width) * extent.Height * extent.Depth * bytesPerTexel;

    const Hikari::Rhi::UniqueHandle<Hikari::Rhi::BufferHandle> readback(
        device, device.CreateBuffer(Hikari::Rhi::BufferDesc{.Size = layerSize * layerCount,
                                                    .Usage = Hikari::Rhi::BufferUsage::CopyDst,
                                                    .Access = Hikari::Rhi::MemoryAccess::GpuToCpu,
                                                    .DebugName = "Texture Readback"}));

    // The source scope is empty for the reason ReadBuffer needs no barrier at
    // all: the upload completed in a submission this thread has already waited
    // on. What the barrier is here for is the layout, which no fence changes.
    const Hikari::Rhi::TextureBarrier toCopySrc{
        .Texture = source,
        .SrcStage = Hikari::Rhi::PipelineStage::None,
        .SrcAccess = Hikari::Rhi::AccessFlags::None,
        .DstStage = Hikari::Rhi::PipelineStage::Copy,
        .DstAccess = Hikari::Rhi::AccessFlags::CopySrc,
        .OldLayout = Hikari::Rhi::TextureLayout::ShaderResource,
        .NewLayout = Hikari::Rhi::TextureLayout::CopySrc,
        .Aspect = Hikari::Rhi::DefaultAspect(pDesc->Format),
        .MipCount = pDesc->MipLevels,
        .LayerCount = layerCount,
    };

    RunGraphicsCommands(
        device,
        [&](Hikari::Rhi::ICommandList& list)
        {
            list.Barrier(toCopySrc);

            for (uint32_t layer = 0; layer < layerCount; layer++)
            {
                list.CopyTextureToBuffer(
                    source, readback.Get(),
                    Hikari::Rhi::BufferTextureCopyRegion{.BufferOffset = layerSize * layer,
                                                 .Aspect = Hikari::Rhi::DefaultAspect(pDesc->Format),
                                                 .MipLevel = mipLevel,
                                                 .BaseLayer = layer,
                                                 .LayerCount = 1u,
                                                 .Extent = extent});
            }
        });

    const auto* pMapped = static_cast<const std::byte*>(device.GetMappedData(readback.Get()));
    REQUIRE(pMapped != nullptr);

    std::vector<std::vector<std::byte>> layers;
    layers.reserve(layerCount);
    for (uint32_t layer = 0; layer < layerCount; layer++)
    {
        const std::byte* pLayer = pMapped + layerSize * layer;
        layers.emplace_back(pLayer, pLayer + layerSize);
    }

    return layers;
}
/**
 * The bytes of a rendered image, tightly packed and row-major.
 *
 * This is what OffscreenTarget::Readback used to do, moved here because nothing
 * outside these tests ever called it — the renderer's own screenshot path
 * stages its copy inside the frame instead.
 *
 * `waitSemaphore` is the target's pending render-complete signal, taken with
 * OffscreenTarget::TakePendingSignal. Passing it explicitly is the point: the
 * wait is what orders this copy after the render that produced the image, and a
 * helper that reached for WaitIdle instead would let a target that established
 * no dependency at all still pass.
 *
 * `currentLayout` is where the last frame left the image. The barrier's source
 * scope names the render target rather than nothing, because a barrier is only
 * ordered after a semaphore wait when its source stage covers the stage waited
 * at.
 */
inline std::vector<std::byte>
ReadRenderedTexture(Hikari::Rhi::IDevice& device, Hikari::Rhi::TextureHandle source,
                    Hikari::Rhi::Extent2D extent, Hikari::Rhi::Format format,
                    Hikari::Rhi::TextureLayout currentLayout,
                    std::optional<Hikari::Rhi::SemaphoreHandle> waitSemaphore)
{
    const uint32_t bytesPerTexel = Hikari::Rhi::BytesPerTexel(format);
    REQUIRE(bytesPerTexel != 0u);

    const uint64_t size = static_cast<uint64_t>(extent.Width) * extent.Height * bytesPerTexel;

    // GpuToCpu rather than CpuToGpu: this is read back randomly by the CPU
    // afterwards, and CpuToGpu may land in write-combined memory where reading
    // is pathologically slow rather than merely uncached.
    const Hikari::Rhi::UniqueHandle<Hikari::Rhi::BufferHandle> staging(
        device, device.CreateBuffer(Hikari::Rhi::BufferDesc{
                    .Size = size,
                    .Usage = Hikari::Rhi::BufferUsage::CopyDst,
                    .Access = Hikari::Rhi::MemoryAccess::GpuToCpu,
                    .DebugName = "Rendered Texture Readback"}));

    const Hikari::Rhi::TextureBarrier toCopySrc{
        .Texture = source,
        .SrcStage = Hikari::Rhi::PipelineStage::RenderTarget,
        .SrcAccess = Hikari::Rhi::AccessFlags::RenderTargetWrite,
        .DstStage = Hikari::Rhi::PipelineStage::Copy,
        .DstAccess = Hikari::Rhi::AccessFlags::CopySrc,
        .OldLayout = currentLayout,
        .NewLayout = Hikari::Rhi::TextureLayout::CopySrc,
        .Aspect = Hikari::Rhi::DefaultAspect(format),
    };

    RunGraphicsCommands(
        device,
        [&](Hikari::Rhi::ICommandList& list)
        {
            list.Barrier(toCopySrc);

            // No BufferOffset and no row length: BufferTextureCopyRegion is
            // tightly packed by definition, which is what makes the returned
            // bytes usable as an image without the caller knowing a stride.
            list.CopyTextureToBuffer(source, staging.Get(),
                                     Hikari::Rhi::BufferTextureCopyRegion{
                                         .Aspect = Hikari::Rhi::DefaultAspect(format),
                                         .Extent = {extent.Width, extent.Height, 1u}});
        },
        waitSemaphore);

    const void* pMapped = device.GetMappedData(staging.Get());
    REQUIRE(pMapped != nullptr);

    // Copied out rather than handed back as a view: the staging buffer is freed
    // on the way out of this function, and a span into it would dangle.
    std::vector<std::byte> bytes(static_cast<size_t>(size));
    std::memcpy(bytes.data(), pMapped, bytes.size());
    return bytes;
}

} // namespace RhiTest
