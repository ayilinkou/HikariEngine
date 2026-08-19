#pragma once

#include <cstring>
#include <stdexcept>
#include <string>

#include "vulkan/vulkan_raii.hpp"

#include <rhi/BufferDesc.h>
#include <rhi/Handles.h>
#include <rhi/IDevice.h>
#include <rhi/UniqueHandle.h>
#include <rhi/vulkan/CommandListUtil.h>
#include <rhi/vulkan/VulkanNative.h>

// Staged uploads, for the callers that need a device-local buffer filled from
// CPU data.
//
// Every one of these submits on its own and drains the queue, which is why they
// are load-time only. An upload context that records many copies behind one
// fence is what should replace them; until it exists, do not call these from
// inside the frame loop.
namespace Rhi::Vulkan
{
// Records a buffer-to-buffer copy, submits it, and waits for it to finish.
inline void CopyBuffer(IDevice& device, vk::raii::CommandPool& commandPool,
                       vk::raii::Queue& transferQueue, BufferHandle source,
                       BufferHandle destination, vk::DeviceSize size)
{
    vk::raii::Device& vkDevice = GetDevice(device);
    vk::raii::CommandBuffer cmd = BeginSingleTimeCommand(vkDevice, commandPool);
    cmd.copyBuffer(GetBuffer(device, source), GetBuffer(device, destination),
                   vk::BufferCopy{0, 0, size});
    EndSingleTimeCommand(cmd, transferQueue);
}

// Creates a device-local buffer holding `pData`, by way of a staging buffer
// that is destroyed before returning.
//
// The staging buffer is held in a UniqueHandle rather than destroyed by hand
// because the copy can throw: the handle model puts the release on the caller,
// and this is exactly the scope-local case where that is a liability rather
// than a feature (plan D2).
[[nodiscard]] inline BufferHandle CreateStagedBuffer(IDevice& device,
                                                     vk::raii::CommandPool& commandPool,
                                                     vk::raii::Queue& transferQueue,
                                                     vk::DeviceSize bufferSize, BufferUsage usage,
                                                     const void* pData, std::string debugName = {})
{
    if (pData == nullptr)
        throw std::runtime_error("CreateStagedBuffer: no source data.");

    UniqueHandle<BufferHandle> staging(
        device, device.CreateBuffer(BufferDesc{.Size = bufferSize,
                                               .Usage = BufferUsage::CopySrc,
                                               .Access = MemoryAccess::CpuToGpu,
                                               .DebugName = debugName.empty()
                                                                ? std::string("Staging Buffer")
                                                                : debugName + " Staging"}));

    void* pMapped = device.GetMappedData(staging.Get());
    if (pMapped == nullptr)
        throw std::runtime_error("CreateStagedBuffer: staging buffer is not host-visible.");

    memcpy(pMapped, pData, static_cast<size_t>(bufferSize));

    const BufferHandle gpuBuffer =
        device.CreateBuffer(BufferDesc{.Size = bufferSize,
                                       .Usage = usage | BufferUsage::CopyDst,
                                       .Access = MemoryAccess::GpuOnly,
                                       .DebugName = std::move(debugName)});

    CopyBuffer(device, commandPool, transferQueue, staging.Get(), gpuBuffer, bufferSize);

    return gpuBuffer;
}
} // namespace Rhi::Vulkan
