#include "vulkan/OwnershipTransfer.h"

#include <limits>

namespace Rhi::Vulkan
{
namespace
{
// The usages that keep an optimal-tiled image on the explicit-transfer path
// even under maintenance9, verbatim from the specification's list (Vulkan 1.4,
// *Resource Sharing*).
//
// They are the attachment usages, and the reason they are excluded is the same
// reason the transfer exists at all: an attachment is the kind of image an
// implementation is most likely to keep in an engine-private compressed form,
// so it is the kind whose contents another engine cannot be promised.
constexpr vk::ImageUsageFlags kAttachmentUsages =
    vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eDepthStencilAttachment |
    vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eInputAttachment |
    vk::ImageUsageFlagBits::eAttachmentFeedbackLoopEXT |
    vk::ImageUsageFlagBits::eFragmentShadingRateAttachmentKHR;

// optimalImageTransferToQueueFamilies is 32 bits wide, so it cannot say
// anything about a family whose index is 32 or higher. Such a device is not
// known to exist, but "the answer is unrepresentable" has to resolve to the
// conservative side rather than to a shift with undefined behaviour.
bool FamilyBitSet(uint32_t mask, uint32_t familyIndex)
{
    if (familyIndex >= std::numeric_limits<uint32_t>::digits)
        return false;

    return (mask & (1u << familyIndex)) != 0u;
}
} // namespace

bool BufferRequiresOwnershipTransfer(const OwnershipTransferRules& rules, uint32_t srcFamily,
                                     uint32_t dstFamily)
{
    // One family cannot hand a resource to itself; the barrier would be a
    // no-op that the specification defines as ignoring both indices.
    if (srcFamily == dstFamily)
        return false;

    // maintenance9 preserves the contents of every buffer across an implicit
    // acquire, with no per-resource conditions at all.
    return !rules.bMaintenance9;
}

bool ImageRequiresOwnershipTransfer(const OwnershipTransferRules& rules, vk::ImageTiling tiling,
                                    vk::ImageUsageFlags usage, uint32_t srcFamily,
                                    uint32_t dstFamily)
{
    if (srcFamily == dstFamily)
        return false;

    if (!rules.bMaintenance9)
        return true;

    // Linear images are laid out exactly as described, so there is nothing an
    // engine could hold them in that another engine would not understand.
    if (tiling == vk::ImageTiling::eLinear)
        return false;

    if (usage & kAttachmentUsages)
        return true;

    return !FamilyBitSet(rules.OptimalImageTransferToQueueFamilies, dstFamily);
}
} // namespace Rhi::Vulkan
