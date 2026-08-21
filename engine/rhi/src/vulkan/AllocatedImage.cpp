#include <rhi/vulkan/AllocatedImage.h>

AllocatedImage::AllocatedImage(VmaAllocator allocator, vk::Image image, VmaAllocation alloc)
    : Image(image), Allocation(alloc), Allocator(allocator)
{
}

AllocatedImage::AllocatedImage(AllocatedImage&& other) noexcept
{
    *this = std::move(other);
}

AllocatedImage& AllocatedImage::operator=(AllocatedImage&& other) noexcept
{
    if (this != &other)
    {
        Destroy();
        Image = other.Image;
        Allocation = other.Allocation;
        Allocator = other.Allocator;
        other.Image = nullptr;
        other.Allocation = nullptr;
        other.Allocator = nullptr;
    }
    return *this;
}

void AllocatedImage::Destroy()
{
    if (Image && Allocation)
        vmaDestroyImage(Allocator, Image, Allocation);
}
