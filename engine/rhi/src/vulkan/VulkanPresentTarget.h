#pragma once

#include <cstdint>

#include <rhi/IPresentTarget.h>

#include "vulkan/VulkanSemaphore.h"

namespace Hikari::Rhi::Vulkan
{
class VulkanDevice;

/** Where one of a target's images is between an Acquire and its Present. */
enum class PresentImageState : uint8_t
{
    Idle,
    Acquired,
    Submitted,
};

/** The binary semaphores a submission writing one acquired image waits on and signals. */
struct PresentSemaphores
{
    /**
     * Waited on before the write: the acquire's, for a swapchain, or the previous
     * write of the same image, for an offscreen target. Invalid when there is
     * nothing to wait for.
     */
    SemaphoreHandle Wait{};

    /** Signalled when the write completes; whatever reads the image next waits on it. */
    SemaphoreHandle Signal{};
};

/**
 * What the device's Submit needs from a Vulkan present target: which device made it,
 * and the semaphores that order a write to one of its images.
 *
 * The caller names only the image (SubmitDesc::PresentImage), so the semaphores are
 * found here instead. Each target tracks where every image is between Acquire,
 * Submit and Present, because a binary semaphore must be unsignalled when a signal
 * reaches the device (VUID-vkQueueSubmit-pSignalSemaphores-00067): an image named
 * by two submissions, or presented unwritten, would break that silently, so both
 * are refused.
 */
class VulkanPresentTarget : public IPresentTarget
{
public:
    virtual bool BelongsTo(const VulkanDevice& device) const = 0;

    /**
     * The semaphores for the one submission writing `index` since its Acquire.
     * Throws for an index out of range, not acquired, or already named by a
     * submission.
     */
    virtual PresentSemaphores TakeSubmitSemaphores(uint32_t index) = 0;
};
} // namespace Hikari::Rhi::Vulkan
