#pragma once

#include "vk_mem_alloc.h"
#include "vulkan/vulkan.hpp"

struct AllocatedBuffer
{
    AllocatedBuffer() = default;
    AllocatedBuffer(VmaAllocator allocator, vk::Buffer buffer, VmaAllocation alloc,
                    const VmaAllocationInfo& info);
    // move-only, like vk::raii types
    AllocatedBuffer(const AllocatedBuffer&) = delete;
    AllocatedBuffer& operator=(const AllocatedBuffer&) = delete;
    AllocatedBuffer(AllocatedBuffer&& other) noexcept;
    AllocatedBuffer& operator=(AllocatedBuffer&& other) noexcept;
    ~AllocatedBuffer() { Destroy(); }

    vk::Buffer Buffer{};
    VmaAllocation Allocation{};
    VmaAllocationInfo AllocationInfo{}; // has .pMappedData if persistently mapped

private:
    void Destroy();

    VmaAllocator Allocator{};
};
