#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
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

// Getting the bytes back off the GPU, which is the only way a test can say what
// an upload actually wrote.
//
// Vulkan-side because submitting is: the RHI hands out a command list (plan D7,
// D8) but not a queue, so recording is neutral and the submission around it is
// not. A second backend's tests would keep these functions' signatures and
// rewrite their bodies.
namespace RhiTest
{
// Records `record` into a command list of its own, submits it to the graphics
// queue, and blocks until the GPU has finished with it.
//
// A pool per call rather than one shared across the binary: these run a handful
// of times per test and the allocation is not what makes them slow, whereas a
// shared pool would need resetting between uses and would make one test's
// failure leave the next one recording into a half-used buffer.
inline void RunGraphicsCommands(Rhi::IDevice& device,
                                const std::function<void(Rhi::ICommandList&)>& record)
{
    vk::raii::Device& vkDevice = Rhi::Vulkan::GetDevice(device);

    const vk::CommandPoolCreateInfo poolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = Rhi::Vulkan::GetGraphicsQueueFamily(device)};
    vk::raii::CommandPool pool(vkDevice, poolInfo);

    const vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = *pool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1u};
    vk::raii::CommandBuffer cmd = std::move(vk::raii::CommandBuffers(vkDevice, allocInfo).front());

    const std::unique_ptr<Rhi::ICommandList> list = Rhi::Vulkan::WrapCommandList(device, *cmd);
    list->Begin();
    record(*list);
    list->End();

    vk::raii::Fence fence(vkDevice, vk::FenceCreateInfo{});

    const vk::CommandBuffer rawCommandBuffer = *cmd;
    const vk::SubmitInfo submitInfo{.commandBufferCount = 1u,
                                    .pCommandBuffers = &rawCommandBuffer};
    Rhi::Vulkan::GetGraphicsQueue(device).submit(submitInfo, *fence);

    const vk::Result result =
        vkDevice.waitForFences(*fence, vk::True, std::numeric_limits<uint64_t>::max());
    REQUIRE(result == vk::Result::eSuccess);
}

// `size` bytes of `source`, copied into a readback buffer and out to the heap.
//
// `source` must carry BufferUsage::CopySrc. No barrier precedes the copy and
// none is needed: whatever filled the buffer did so in an earlier submission
// that a fence wait has already returned from, and a fence signal's access
// scope is every access the device performed (Vulkan 1.4, *Fences*) — which is
// the same guarantee IUploadContext::Flush relies on for the renderer.
inline std::vector<std::byte> ReadBuffer(Rhi::IDevice& device, Rhi::BufferHandle source,
                                         uint64_t size)
{
    const Rhi::UniqueHandle<Rhi::BufferHandle> readback(
        device, device.CreateBuffer(Rhi::BufferDesc{.Size = size,
                                                    .Usage = Rhi::BufferUsage::CopyDst,
                                                    .Access = Rhi::MemoryAccess::GpuToCpu,
                                                    .DebugName = "Readback"}));

    RunGraphicsCommands(device,
                        [&](Rhi::ICommandList& list)
                        {
                            list.CopyBuffer(source, readback.Get(),
                                            Rhi::BufferCopyRegion{.Size = size});
                        });

    const void* pMapped = device.GetMappedData(readback.Get());
    REQUIRE(pMapped != nullptr);

    std::vector<std::byte> bytes(static_cast<size_t>(size));
    std::memcpy(bytes.data(), pMapped, bytes.size());
    return bytes;
}

// Mip `mipLevel` of every array layer of `source`, one tightly packed entry per
// layer.
//
// One call rather than one per layer because the layout transition has to cover
// the whole texture: a layout is a property of a subresource, and transitioning
// them one at a time would leave the rest where they were. Each layer is
// nonetheless copied by its own region naming BaseLayer, which is what makes an
// upload that wrote every face into layer 0 show up as five wrong layers rather
// than as one buffer that happens to hold the right bytes somewhere.
//
// `source` must carry TextureUsage::CopySrc and be in the ShaderResource layout
// — which is where IUploadContext leaves everything it fills. The texture is
// left in CopySrc.
inline std::vector<std::vector<std::byte>>
ReadTextureLayers(Rhi::IDevice& device, Rhi::TextureHandle source, uint32_t mipLevel = 0u)
{
    const Rhi::TextureDesc* pDesc = device.GetTextureDesc(source);
    REQUIRE(pDesc != nullptr);

    const Rhi::Extent3D extent{std::max(pDesc->Extent.Width >> mipLevel, 1u),
                               std::max(pDesc->Extent.Height >> mipLevel, 1u),
                               std::max(pDesc->Extent.Depth >> mipLevel, 1u)};

    const uint32_t layerCount = pDesc->ArrayLayers;

    // Zero means the format has no single texel size, which for a combined
    // depth/stencil format is the truth rather than a failure — this helper
    // copies one aspect and cannot pick. No test needs that today, so it is a
    // hard stop rather than an extra parameter nothing would pass.
    const uint32_t bytesPerTexel = Rhi::BytesPerTexel(pDesc->Format);
    REQUIRE(bytesPerTexel != 0u);

    const uint64_t layerSize =
        static_cast<uint64_t>(extent.Width) * extent.Height * extent.Depth * bytesPerTexel;

    const Rhi::UniqueHandle<Rhi::BufferHandle> readback(
        device, device.CreateBuffer(Rhi::BufferDesc{.Size = layerSize * layerCount,
                                                    .Usage = Rhi::BufferUsage::CopyDst,
                                                    .Access = Rhi::MemoryAccess::GpuToCpu,
                                                    .DebugName = "Texture Readback"}));

    // The source scope is empty for the reason ReadBuffer needs no barrier at
    // all: the upload completed in a submission this thread has already waited
    // on. What the barrier is here for is the layout, which no fence changes.
    const Rhi::TextureBarrier toCopySrc{
        .Texture = source,
        .SrcStage = Rhi::PipelineStage::None,
        .SrcAccess = Rhi::AccessFlags::None,
        .DstStage = Rhi::PipelineStage::Copy,
        .DstAccess = Rhi::AccessFlags::CopySrc,
        .OldLayout = Rhi::TextureLayout::ShaderResource,
        .NewLayout = Rhi::TextureLayout::CopySrc,
        .Aspect = Rhi::DefaultAspect(pDesc->Format),
        .MipCount = pDesc->MipLevels,
        .LayerCount = layerCount,
    };

    RunGraphicsCommands(
        device,
        [&](Rhi::ICommandList& list)
        {
            list.Barrier(toCopySrc);

            for (uint32_t layer = 0; layer < layerCount; layer++)
            {
                list.CopyTextureToBuffer(
                    source, readback.Get(),
                    Rhi::BufferTextureCopyRegion{.BufferOffset = layerSize * layer,
                                                 .Aspect = Rhi::DefaultAspect(pDesc->Format),
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
} // namespace RhiTest
