#pragma once

#include <cstdint>

#include <rhi/IPresentTarget.h>

namespace Hikari::Rhi::D3D12
{
class D3D12Device;

/** Where one of a target's images is between an Acquire and its Present. */
enum class PresentImageState : uint8_t
{
    Idle,
    Acquired,
    Submitted,
};

/**
 * What the device's Submit needs from a D3D12 present target: which device made it,
 * and a record that the image's one submission was made.
 *
 * D3D12 needs nothing to order the write itself — a queue runs its lists in the order
 * they were submitted, and a present is queued behind the rendering already on its
 * queue — so what remains is the seam's contract: one submission per acquired image,
 * before its Present. Both targets refuse a break of it, as the Vulkan ones must, so
 * that a caller correct on one backend is correct on the other.
 */
class D3D12PresentTarget : public IPresentTarget
{
public:
    virtual bool BelongsTo(const D3D12Device& device) const = 0;

    /**
     * Records the submission writing `index`. Throws for an index out of range, not
     * acquired, or already named by a submission since its Acquire.
     */
    virtual void MarkSubmitted(uint32_t index) = 0;
};
} // namespace Hikari::Rhi::D3D12
