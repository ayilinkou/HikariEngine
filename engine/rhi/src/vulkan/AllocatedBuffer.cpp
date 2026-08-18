#include <rhi/vulkan/AllocatedBuffer.h>

AllocatedBuffer::AllocatedBuffer(VmaAllocator allocator, vk::Buffer buffer, VmaAllocation alloc,
                                 const VmaAllocationInfo& info)
    : Buffer(buffer), Allocation(alloc), AllocationInfo(info), Allocator(allocator)
{
}

AllocatedBuffer::AllocatedBuffer(AllocatedBuffer&& other) noexcept
{
    *this = std::move(other);
}

AllocatedBuffer& AllocatedBuffer::operator=(AllocatedBuffer&& other) noexcept
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

void AllocatedBuffer::Destroy()
{
    if (Buffer && Allocation)
        vmaDestroyBuffer(Allocator, Buffer, Allocation);
}
