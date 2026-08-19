#pragma once

#include <cstdint>
#include <span>

#include "vulkan/vulkan.hpp"

#include <rhi/Barrier.h>

struct AllocatedImage;

// Records neutral barriers (<rhi/Barrier.h>, <rhi/BarrierPresets.h>) into a
// Vulkan command buffer.
//
// This is the only place a neutral barrier becomes a VkImageMemoryBarrier2, so
// it is also the only place the queue-family fields are set — both are
// VK_QUEUE_FAMILY_IGNORED here, because every barrier the renderer currently
// records stays on one queue.
namespace Rhi::Vulkan
{
// A barrier and the image it applies to.
//
// The pairing exists on the Vulkan side because a texture is still identified
// by its VkImage; once a texture is named by a handle the identity belongs in
// TextureBarrier itself and this struct goes away.
struct ImageBarrier
{
    vk::Image Image;
    TextureBarrier Barrier;
};

// Records every barrier in `barriers` as one vkCmdPipelineBarrier2, and returns
// what that cost — the barrier count, and the one call it took. An empty span
// records nothing and returns zero of both.
//
// Grouping matters beyond saving call overhead. Each vkCmdPipelineBarrier2 is
// its own execution dependency, so transitioning three textures in three calls
// orders those three transitions against one another for no reason — the
// driver may not begin the second until the first has completed. Issued
// together they are independent, which is what they actually are.
//
// A caller with barriers separated by real work (a copy, a dispatch) must still
// issue them separately; the dependency is the point in that case.
BarrierCounts RecordBarriers(vk::CommandBuffer cmd, std::span<const ImageBarrier> barriers);

// Single-barrier shorthand, for the sites that have nothing to group with.
// Returns one of each, so that a caller keeping a running total adds it the
// same way it adds a batch rather than hard-coding numbers beside the call.
BarrierCounts RecordBarrier(vk::CommandBuffer cmd, vk::Image image, const TextureBarrier& barrier);
BarrierCounts RecordBarrier(vk::CommandBuffer cmd, const AllocatedImage& image,
                            const TextureBarrier& barrier);
} // namespace Rhi::Vulkan
