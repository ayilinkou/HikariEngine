#pragma once

#include <cstdint>

#include "vulkan/vulkan.hpp"

namespace Rhi::Vulkan
{
// What a logical device ended up able to promise about queue family ownership
// transfers, as a value, so that the rules below are pure functions of it.
//
// Everything here is a property of one *source* family — the family that
// currently owns the resource and is handing it on. A device holds one of these
// per family; a caller asks for the one belonging to the queue it recorded the
// work on.
struct OwnershipTransferRules
{
    // Whether VK_KHR_maintenance9's feature was enabled on the logical device.
    // Supported-but-not-enabled counts as false: every relaxation the extension
    // describes is worded "if the maintenance9 feature is enabled", so enabling
    // the extension alone changes nothing.
    bool bMaintenance9 = false;

    // VkQueueFamilyOwnershipTransferPropertiesKHR::optimalImageTransferToQueueFamilies
    // for the source family: a bitmask of *destination* family indices that can
    // implicitly acquire an optimal-tiled image from it with its contents
    // intact. Meaningless unless bMaintenance9.
    uint32_t OptimalImageTransferToQueueFamilies = 0u;
};

// Whether a buffer range written on `srcFamily` must be explicitly released to
// `dstFamily` before that family can rely on its contents.
bool BufferRequiresOwnershipTransfer(const OwnershipTransferRules& rules, uint32_t srcFamily,
                                     uint32_t dstFamily);

// The same question for an image, which maintenance9 answers per resource
// rather than blanket: `tiling` and `usage` are the image's own, as passed to
// vkCreateImage.
bool ImageRequiresOwnershipTransfer(const OwnershipTransferRules& rules, vk::ImageTiling tiling,
                                    vk::ImageUsageFlags usage, uint32_t srcFamily,
                                    uint32_t dstFamily);
} // namespace Rhi::Vulkan
