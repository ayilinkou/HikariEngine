#include "vulkan/VulkanBuffer.h"

namespace Rhi::Vulkan
{
VulkanBuffer::VulkanBuffer(VmaAllocator allocator, vk::Buffer buffer, VmaAllocation alloc,
                           const VmaAllocationInfo& info)
    : Buffer(buffer), Allocation(alloc), AllocationInfo(info), Allocator(allocator)
{
}

VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept
{
    *this = std::move(other);
}

VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept
{
    if (this != &other)
    {
        Destroy();
        Buffer = other.Buffer;
        Allocation = other.Allocation;
        AllocationInfo = other.AllocationInfo;
        Allocator = other.Allocator;
        other.Buffer = nullptr;
        other.Allocation = nullptr;
        other.Allocator = nullptr;
    }
    return *this;
}

void VulkanBuffer::Destroy()
{
    if (Buffer && Allocation)
        vmaDestroyBuffer(Allocator, Buffer, Allocation);
}
} // namespace Rhi::Vulkan
