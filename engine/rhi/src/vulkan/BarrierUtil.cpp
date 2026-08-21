#include <rhi/vulkan/BarrierUtil.h>

#include <array>
#include <vector>

#include <rhi/vulkan/AllocatedImage.h>

#include "vulkan/VulkanConversions.h"

namespace Rhi::Vulkan
{
namespace
{
// Not an overload of ToVk: an unqualified call inside this anonymous namespace
// would find only the name declared here and never reach the conversion table's
// overloads in the enclosing namespace.
vk::ImageMemoryBarrier2 MakeVkBarrier(const ImageBarrier& barrier)
{
    const TextureBarrier& desc = barrier.Barrier;

    const vk::ImageSubresourceRange range{.aspectMask = ToVk(desc.Aspect),
                                          .baseMipLevel = desc.BaseMip,
                                          .levelCount = desc.MipCount,
                                          .baseArrayLayer = desc.BaseLayer,
                                          .layerCount = desc.LayerCount};

    return vk::ImageMemoryBarrier2{.srcStageMask = ToVk(desc.SrcStage),
                                   .srcAccessMask = ToVk(desc.SrcAccess),
                                   .dstStageMask = ToVk(desc.DstStage),
                                   .dstAccessMask = ToVk(desc.DstAccess),
                                   .oldLayout = ToVk(desc.OldLayout),
                                   .newLayout = ToVk(desc.NewLayout),
                                   .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                   .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                   .image = barrier.Image,
                                   .subresourceRange = range};
}
} // namespace

BarrierCounts RecordBarriers(vk::CommandBuffer cmd, std::span<const ImageBarrier> barriers)
{
    if (barriers.empty())
        return {};

    std::vector<vk::ImageMemoryBarrier2> converted;
    converted.reserve(barriers.size());
    for (const ImageBarrier& barrier : barriers)
        converted.push_back(MakeVkBarrier(barrier));

    const vk::DependencyInfo dependencyInfo{.imageMemoryBarrierCount =
                                                static_cast<uint32_t>(converted.size()),
                                            .pImageMemoryBarriers = converted.data()};
    cmd.pipelineBarrier2(dependencyInfo);

    return BarrierCounts{.Barriers = static_cast<uint32_t>(barriers.size()), .Calls = 1u};
}

BarrierCounts RecordBarrier(vk::CommandBuffer cmd, vk::Image image, const TextureBarrier& barrier)
{
    const std::array one{ImageBarrier{.Image = image, .Barrier = barrier}};
    return RecordBarriers(cmd, one);
}

BarrierCounts RecordBarrier(vk::CommandBuffer cmd, const AllocatedImage& image,
                            const TextureBarrier& barrier)
{
    return RecordBarrier(cmd, image.Image, barrier);
}
} // namespace Rhi::Vulkan
