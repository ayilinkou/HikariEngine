#pragma once

#include <cstdint>
#include <span>
#include <string>

#include <rhi/Handles.h>
#include <rhi/RhiTypes.h>

namespace Hikari::Rhi
{
class ICommandList;
class IPresentTarget;

/**
 * A fence, and the value it is waited on or signalled to reach.
 *
 * Fences are monotonic counters rather than the signalled/unsignalled flags
 * Vulkan's VkFence uses, because that is the only primitive D3D12 has: an
 * ID3D12Fence and a value. Vulkan's equivalent is a timeline semaphore, core
 * since 1.2. Modelling the intersection means a wait can name a point in the
 * past and return immediately, which is what makes "wait for the frame that
 * used this slot" expressible without resetting anything (plan D5).
 */
struct FenceOperation
{
    FenceHandle Fence{};
    uint64_t Value = 0u;
};

struct FenceDesc
{
    /** The counter's starting point. Waits for this value or lower return at once. */
    uint64_t InitialValue = 0u;

    std::string DebugName;
};

/**
 * The image of a present target that a submission writes: the target, and the
 * index its Acquire handed out.
 */
struct PresentTargetImage
{
    IPresentTarget* pTarget = nullptr;
    uint32_t Index = 0u;
};

/**
 * One submission to one queue.
 *
 * Lists execute in the order given. Every list must have been ended, and every
 * one must have come from an allocator created for this queue type.
 */
struct SubmitDesc
{
    QueueType Queue = QueueType::Graphics;

    std::span<ICommandList* const> CommandLists{};

    std::span<const FenceOperation> WaitFences{};
    std::span<const FenceOperation> SignalFences{};

    /**
     * The acquired image these lists write, or no target when they write none.
     *
     * What a frame knows is which image it draws into, and that is all a backend
     * needs to order the write: Vulkan's present path orders it with binary
     * semaphores the target owns, and D3D12 needs nothing, since a present is
     * queued behind the rendering already on its queue. Exactly one submission
     * names each acquired image, between its Acquire and its Present, and the
     * target must be this device's.
     */
    PresentTargetImage PresentImage{};
};
} // namespace Hikari::Rhi
