#include "vulkan/VulkanCommandList.h"

#include <array>
#include <format>
#include <vector>

#include "vulkan/VulkanConversions.h"
#include "vulkan/VulkanDevice.h"

namespace Hikari::Rhi::Vulkan
{
namespace
{
// Not an overload of ToVk: an unqualified call inside this anonymous namespace
// would find only the name declared here and never reach the conversion table's
// overloads in the enclosing namespace.
vk::ImageMemoryBarrier2 MakeVkBarrier(vk::Image image, const TextureBarrier& desc)
{
    const vk::ImageSubresourceRange range{.aspectMask = ToVk(desc.Aspect),
                                          .baseMipLevel = desc.BaseMip,
                                          .levelCount = desc.MipCount,
                                          .baseArrayLayer = desc.BaseLayer,
                                          .layerCount = desc.LayerCount};

    // Both queue-family fields are IGNORED, which is what a barrier that stays
    // on one queue must say. A neutral TextureBarrier cannot describe a queue
    // family ownership transfer at all — the concept has no D3D12 counterpart
    // (plan D6) — so the component that submits to a second queue builds those
    // barriers itself; VulkanUploadContext is the one that does.
    return vk::ImageMemoryBarrier2{.srcStageMask = ToVk(desc.SrcStage),
                                   .srcAccessMask = ToVk(desc.SrcAccess),
                                   .dstStageMask = ToVk(desc.DstStage),
                                   .dstAccessMask = ToVk(desc.DstAccess),
                                   .oldLayout = ToVk(desc.OldLayout),
                                   .newLayout = ToVk(desc.NewLayout),
                                   .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                   .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                   .image = image,
                                   .subresourceRange = range};
}

vk::BufferImageCopy MakeVkCopy(const BufferTextureCopyRegion& region)
{
    // bufferRowLength and bufferImageHeight of 0 mean "tightly packed to the
    // image extent", which is the only layout ICommandList's region describes.
    return vk::BufferImageCopy{.bufferOffset = region.BufferOffset,
                               .bufferRowLength = 0u,
                               .bufferImageHeight = 0u,
                               .imageSubresource = {.aspectMask = ToVk(region.Aspect),
                                                    .mipLevel = region.MipLevel,
                                                    .baseArrayLayer = region.BaseLayer,
                                                    .layerCount = region.LayerCount},
                               .imageOffset = {0, 0, 0},
                               .imageExtent = vk::Extent3D{
                                   region.Extent.Width, region.Extent.Height, region.Extent.Depth}};
}
} // namespace

VulkanCommandList::VulkanCommandList(VulkanDevice& device, vk::CommandBuffer cmd)
    : m_Device(device), m_Cmd(cmd)
{
}

void VulkanCommandList::Begin()
{
    m_Cmd.begin(vk::CommandBufferBeginInfo{});
}

void VulkanCommandList::End()
{
    m_Cmd.end();
}

BarrierCounts VulkanCommandList::Barrier(std::span<const TextureBarrier> barriers)
{
    if (barriers.empty())
        return {};

    std::vector<vk::ImageMemoryBarrier2> converted;
    converted.reserve(barriers.size());

    for (const TextureBarrier& barrier : barriers)
    {
        const vk::Image image = m_Device.GetImage(barrier.Texture);
        if (!image)
        {
            // Dropped rather than recorded against a null image, which the
            // driver would reject. The barrier counts in the run report go down
            // by one when this happens, which is the point of reporting them.
            m_Device.ReportStaleHandle(
                std::format("Rhi::ICommandList::Barrier: texture handle {:#010x} is stale or was "
                            "never valid; the barrier was not recorded.",
                            barrier.Texture.Value));
            continue;
        }

        converted.push_back(MakeVkBarrier(image, barrier));
    }

    if (converted.empty())
        return {};

    const vk::DependencyInfo dependencyInfo{.imageMemoryBarrierCount =
                                                static_cast<uint32_t>(converted.size()),
                                            .pImageMemoryBarriers = converted.data()};
    m_Cmd.pipelineBarrier2(dependencyInfo);

    return BarrierCounts{.Barriers = static_cast<uint32_t>(converted.size()), .Calls = 1u};
}

BarrierCounts VulkanCommandList::Barrier(const TextureBarrier& barrier)
{
    const std::array one{barrier};
    return Barrier(one);
}

void VulkanCommandList::CopyBuffer(BufferHandle source, BufferHandle destination,
                                   const BufferCopyRegion& region)
{
    const vk::Buffer src = m_Device.GetBuffer(source);
    const vk::Buffer dst = m_Device.GetBuffer(destination);
    if (!src || !dst)
    {
        m_Device.ReportStaleHandle(
            std::format("Rhi::ICommandList::CopyBuffer: buffer handle {:#010x} or {:#010x} is "
                        "stale; the copy was not recorded.",
                        source.Value, destination.Value));
        return;
    }

    m_Cmd.copyBuffer(src, dst,
                     vk::BufferCopy{.srcOffset = region.SrcOffset,
                                    .dstOffset = region.DstOffset,
                                    .size = region.Size});
}

void VulkanCommandList::CopyBufferToTexture(BufferHandle source, TextureHandle destination,
                                            const BufferTextureCopyRegion& region)
{
    const vk::Buffer src = m_Device.GetBuffer(source);
    const vk::Image dst = m_Device.GetImage(destination);
    if (!src || !dst)
    {
        m_Device.ReportStaleHandle(
            std::format("Rhi::ICommandList::CopyBufferToTexture: buffer handle {:#010x} or "
                        "texture handle {:#010x} is stale; the copy was not recorded.",
                        source.Value, destination.Value));
        return;
    }

    // The destination has to already be in CopyDst; see ICommandList's comment
    // on why a copy transitions nothing itself.
    m_Cmd.copyBufferToImage(src, dst, vk::ImageLayout::eTransferDstOptimal, MakeVkCopy(region));
}

void VulkanCommandList::CopyTextureToBuffer(TextureHandle source, BufferHandle destination,
                                            const BufferTextureCopyRegion& region)
{
    const vk::Image src = m_Device.GetImage(source);
    const vk::Buffer dst = m_Device.GetBuffer(destination);
    if (!src || !dst)
    {
        m_Device.ReportStaleHandle(
            std::format("Rhi::ICommandList::CopyTextureToBuffer: texture handle {:#010x} or "
                        "buffer handle {:#010x} is stale; the copy was not recorded.",
                        source.Value, destination.Value));
        return;
    }

    m_Cmd.copyImageToBuffer(src, vk::ImageLayout::eTransferSrcOptimal, dst, MakeVkCopy(region));
}
} // namespace Hikari::Rhi::Vulkan
