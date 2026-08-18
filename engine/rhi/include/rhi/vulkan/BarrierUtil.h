#pragma once

#include "vulkan/vulkan_raii.hpp"

#include <rhi/vulkan/AllocatedImage.h>
#include <rhi/vulkan/Barrier.h>

// Records one of <rhi/vulkan/Barrier.h>'s presets into a command list.
//
// Both this and the presets it consumes are due to be replaced by a neutral
// barrier API built on the PipelineStage/AccessFlags/TextureLayout vocabulary in
// <rhi/Barrier.h>.

// TODO: collect barriers and group them into a single pipelineBarrier2 call
inline void RecordImageBarrier(vk::raii::CommandBuffer& cmd, vk::Image image,
                               const ImageBarrierDesc& desc)
{
    const vk::ImageSubresourceRange range{.aspectMask = desc.aspect,
                                          .baseMipLevel = desc.baseMip,
                                          .levelCount = desc.mipCount,
                                          .baseArrayLayer = desc.baseLayer,
                                          .layerCount = desc.layerCount};

    const vk::ImageMemoryBarrier2 barrier{.srcStageMask = desc.srcStage,
                                          .srcAccessMask = desc.srcAccess,
                                          .dstStageMask = desc.dstStage,
                                          .dstAccessMask = desc.dstAccess,
                                          .oldLayout = desc.oldLayout,
                                          .newLayout = desc.newLayout,
                                          .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                          .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                          .image = image,
                                          .subresourceRange = range};

    vk::DependencyInfo dependencyInfo{.imageMemoryBarrierCount = 1,
                                      .pImageMemoryBarriers = &barrier};
    cmd.pipelineBarrier2(dependencyInfo);
}

inline void RecordImageBarrier(vk::raii::CommandBuffer& cmd, const AllocatedImage& image,
                               const ImageBarrierDesc& desc)
{
    RecordImageBarrier(cmd, image.Image, desc);
}
