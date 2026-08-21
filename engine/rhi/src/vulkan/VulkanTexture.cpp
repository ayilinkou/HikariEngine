#include "vulkan/VulkanTexture.h"

namespace Rhi::Vulkan
{
VulkanTexture::VulkanTexture(VmaAllocator allocator, vk::Image image, VmaAllocation alloc,
                             TextureDesc desc)
    : Image(image), Allocation(alloc), Desc(std::move(desc)), Allocator(allocator)
{
}

VulkanTexture::VulkanTexture(VulkanTexture&& other) noexcept
{
    *this = std::move(other);
}

VulkanTexture& VulkanTexture::operator=(VulkanTexture&& other) noexcept
{
    if (this != &other)
    {
        Destroy();
        Image = other.Image;
        Allocation = other.Allocation;
        Desc = std::move(other.Desc);
        Allocator = other.Allocator;
        other.Image = nullptr;
        other.Allocation = nullptr;
        other.Allocator = nullptr;
    }
    return *this;
}

void VulkanTexture::Destroy()
{
    // A null Allocation is a borrowed image — a swapchain one — and freeing it
    // would destroy something the presentation engine owns.
    if (Image && Allocation)
        vmaDestroyImage(Allocator, Image, Allocation);
}
} // namespace Rhi::Vulkan
