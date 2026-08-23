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
// a monotonically increasing value, and Vulkan's timeline semaphore matches it.
//
// Declared ahead of its first user, which is why nothing takes one yet: the
// upload context waits on a fence it owns and never shows it, and the frame
// loop's synchronization still belongs to the application.
using FenceHandle = Handle<struct FenceTag>;

// A GPU-to-GPU ordering point with no value attached, and the counterpart to
// FenceHandle rather than a lesser version of it: presentation is the one place
// both APIs still order work by a single-shot object the caller never resets.
//
// Only IPresentTarget produces one. The target owns the object, decides how many
// there are and when they are recycled; a handle is how a caller names one for
// long enough to wait on or signal it in its own submit. Nothing else in the RHI
// hands out a semaphore, and nothing should — a caller that wants ordering
// against RHI-owned work wants a fence and a value.
using SemaphoreHandle = Handle<struct SemaphoreTag>;
} // namespace Rhi
