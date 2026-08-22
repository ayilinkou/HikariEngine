#pragma once

#include "vulkan/vulkan_raii.hpp"

#include <rhi/vulkan/DebugNames.h>

// Allocate-begin-submit-wait for a one-shot command list.
//
// EndSingleTimeCommand blocks on queue idle, which is the bluntest possible
// synchronisation and stalls the submitting thread completely. That is
// acceptable only because every current caller is a load-time upload. An upload
// context that records many copies behind a single fence is what should replace
// this; until one exists, do not call these from inside the frame loop.

[[nodiscard]] inline vk::raii::CommandBuffer BeginSingleTimeCommand(vk::raii::Device& device,
                                                                    vk::CommandPool commandPool)
{
    vk::CommandBufferAllocateInfo allocInfo{.commandPool = commandPool,
                                            .level = vk::CommandBufferLevel::ePrimary,
                                            .commandBufferCount = 1};
    vk::raii::CommandBuffer commandBuffer =
        std::move(device.allocateCommandBuffers(allocInfo).front());
    SetVkDebugName(device, *commandBuffer, vk::ObjectType::eCommandBuffer,
                   "Single Use Command Buffer");
    vk::CommandBufferBeginInfo beginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    commandBuffer.begin(beginInfo);
    return commandBuffer;
}

inline void EndSingleTimeCommand(vk::CommandBuffer commandBuffer, vk::Queue queue)
{
    commandBuffer.end();
    vk::SubmitInfo submitInfo{.commandBufferCount = 1, .pCommandBuffers = &commandBuffer};
    queue.submit(submitInfo, nullptr);
    queue.waitIdle();
}
