#pragma once

#include <core/Handle.h>

// The identities that cross the RHI boundary. A caller holds one of these
// rather than a pointer to a backend object, so nothing outside the module
// needs to know that a texture is a VkImage plus a VmaAllocation (plan D2).
//
// Each is a distinct type, so a buffer handle cannot be passed where a texture
// handle is expected. The tags are declared inline and never defined — they
// exist only to separate the types.
namespace Rhi
{
using BufferHandle = Handle<struct BufferTag>;
using TextureHandle = Handle<struct TextureTag>;
using TextureViewHandle = Handle<struct TextureViewTag>;
using SamplerHandle = Handle<struct SamplerTag>;

// Paired with a uint64_t value at every use site rather than being waited on
// by itself: D3D12 has exactly one synchronization primitive, ID3D12Fence with
// a monotonically increasing value, and Vulkan's timeline semaphore matches it
// (plan D5). The binary semaphores the swapchain requires are deliberately
// absent — they stay an implementation detail of the present path.
using FenceHandle = Handle<struct FenceTag>;
} // namespace Rhi
