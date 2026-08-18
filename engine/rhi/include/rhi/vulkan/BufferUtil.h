#pragma once

#include <cstring>
#include <stdexcept>

#include "vk_mem_alloc.h"
#include "vulkan/vulkan_raii.hpp"

#include <rhi/vulkan/AllocatedBuffer.h>
#include <rhi/vulkan/CommandListUtil.h>

// VMA-backed buffer creation and staged uploads.
//
// These take the raw VmaAllocator rather than VulkanAllocator&, because
// VulkanAllocator converts implicitly and the callers hold both kinds. Once the
// device owns the allocator these become members of it and the parameter goes
// away.

[[nodiscard]] inline AllocatedBuffer CreateBuffer(VmaAllocator allocator, vk::DeviceSize size,
                                                  vk::BufferUsageFlags bufferUsage,
                                                  VmaMemoryUsage memoryUsage,
                                                  VmaAllocationCreateFlags allocFlags = 0)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = static_cast<VkDeviceSize>(size);
    bufferInfo.usage = static_cast<VkBufferUsageFlags>(bufferUsage);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;
    allocInfo.flags = allocFlags;

    VkBuffer rawBuffer;
    VmaAllocation allocation;
    VmaAllocationInfo allocationInfo;

    vk::Result result = static_cast<vk::Result>(vmaCreateBuffer(
        allocator, &bufferInfo, &allocInfo, &rawBuffer, &allocation, &allocationInfo));

    if (result != vk::Result::eSuccess)
        throw std::runtime_error("Failed to create buffer via VMA!");

    return AllocatedBuffer(allocator, vk::Buffer(rawBuffer), allocation, allocationInfo);
}

inline void CopyBuffer(vk::raii::Device& device, vk::raii::CommandPool& commandPool,
                       vk::raii::Queue& transferQueue, vk::Buffer srcBuffer, vk::Buffer dstBuffer,
                       vk::DeviceSize size)
{
    vk::raii::CommandBuffer commandCopyBuffer = BeginSingleTimeCommand(device, commandPool);
    commandCopyBuffer.copyBuffer(srcBuffer, dstBuffer, vk::BufferCopy{0, 0, size});
    EndSingleTimeCommand(commandCopyBuffer, transferQueue);
}

[[nodiscard]] inline AllocatedBuffer
CreateStagedBuffer(VmaAllocator allocator, vk::raii::Device& device,
                   vk::raii::CommandPool& commandPool, vk::raii::Queue& transferQueue,
                   vk::DeviceSize bufferSize, vk::BufferUsageFlags usage, void* pData)
{
    AllocatedBuffer stagingBuffer = CreateBuffer(
        allocator, bufferSize, vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

    memcpy(stagingBuffer.AllocationInfo.pMappedData, pData, static_cast<size_t>(bufferSize));

    AllocatedBuffer gpuBuffer =
        CreateBuffer(allocator, bufferSize, usage | vk::BufferUsageFlagBits::eTransferDst,
                     VMA_MEMORY_USAGE_AUTO);

    CopyBuffer(device, commandPool, transferQueue, vk::Buffer(stagingBuffer.Buffer),
               vk::Buffer(gpuBuffer.Buffer), bufferSize);

    return gpuBuffer;
}
