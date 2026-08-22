#include "vulkan/QueueFamilies.h"

#include <bit>
#include <stdexcept>

#include "vulkan/VulkanConversions.h"

namespace Rhi::Vulkan
{
namespace
{
// How many of graphics/compute/copy a family advertises, as a stand-in for how
// heavyweight the engine behind it is. The ancillary bits — sparse binding,
// protected memory, video, optical flow — are deliberately not counted: they
// say nothing about that, and counting them would let a video family with one
// extra bit outrank a transfer-only one.
int CoreCapabilityCount(vk::QueueFlags capabilities)
{
    constexpr vk::QueueFlags kCore =
        vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eTransfer;

    return std::popcount(static_cast<uint32_t>(capabilities & kCore));
}

// The narrowest family that can serve `role` without also being a graphics
// family, or kInvalid when the device exposes none.
uint32_t FindDedicatedFamily(std::span<const vk::QueueFamilyProperties> families, QueueType role)
{
    uint32_t best = QueueFamilies::kInvalid;
    int bestCapabilityCount = 0;

    for (uint32_t index = 0; index < families.size(); index++)
    {
        const vk::QueueFlags capabilities = families[index].queueFlags;

        if (!FamilySupports(capabilities, role) ||
            FamilySupports(capabilities, QueueType::Graphics))
            continue;

        const int capabilityCount = CoreCapabilityCount(capabilities);
        if (best == QueueFamilies::kInvalid || capabilityCount < bestCapabilityCount)
        {
            best = index;
            bestCapabilityCount = capabilityCount;
        }
    }

    return best;
}
} // namespace

uint32_t QueueFamilies::Get(QueueType role) const
{
    switch (role)
    {
        case QueueType::Graphics:
            return Graphics;
        case QueueType::Compute:
            return Compute;
        case QueueType::Copy:
            return Copy;
    }

    throw std::runtime_error("Rhi::Vulkan::QueueFamilies::Get(): unhandled QueueType enumerator.");
}

bool QueueFamilies::IsDedicated(QueueType role) const
{
    const uint32_t family = Get(role);
    return family != kInvalid && family != Graphics;
}

QueueFamilies SelectQueueFamilies(std::span<const vk::QueueFamilyProperties> families,
                                  const PresentSupportFn& presentSupported, bool bRequirePresent,
                                  bool bForceSingleQueue)
{
    QueueFamilies result;

    for (uint32_t index = 0; index < families.size(); index++)
    {
        if (!FamilySupports(families[index].queueFlags, QueueType::Graphics))
            continue;

        if (bRequirePresent && !(presentSupported && presentSupported(index)))
            continue;

        result.Graphics = index;
        break;
    }

    // Leaving both unresolved is what makes the fallback below assign them the
    // graphics family, so forcing a single queue is the absence of a search
    // rather than a second assignment that would have to stay in step with it.
    if (!bForceSingleQueue)
    {
        result.Compute = FindDedicatedFamily(families, QueueType::Compute);
        result.Copy = FindDedicatedFamily(families, QueueType::Copy);
    }

    if (result.Graphics == QueueFamilies::kInvalid)
        return result;

    const vk::QueueFlags graphicsCapabilities = families[result.Graphics].queueFlags;

    // With nothing better available, fall back to the family that is capable
    // anyway. Copy always is — advertising graphics is enough to be able to
    // copy — whereas a graphics family that omits compute genuinely cannot run
    // a dispatch, so that role stays unresolved rather than pointing somewhere
    // that would fail at submission.
    if (result.Compute == QueueFamilies::kInvalid &&
        FamilySupports(graphicsCapabilities, QueueType::Compute))
        result.Compute = result.Graphics;

    if (result.Copy == QueueFamilies::kInvalid)
        result.Copy = result.Graphics;

    return result;
}
} // namespace Rhi::Vulkan
