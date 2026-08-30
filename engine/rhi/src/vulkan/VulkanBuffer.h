#pragma once

#include <utility>

#include "vk_mem_alloc.h"
#include "vulkan/vulkan.hpp"

namespace Hikari::Rhi::Vulkan
{
/**
 * What a BufferHandle resolves to: the VMA-owned buffer itself.
 *
 * Module-private, and the payload of the device's Core::HandlePool. RAII is not
 * banished by the handle model, only kept off the public seam (plan D2) — this
 * still frees itself, which is what makes releasing a pool slot enough to free
 * the allocation.
 *
 * Default-constructible and move-assignable because Core::HandlePool requires both: a
 * free slot holds a default-constructed payload, and Release() assigns one over
 * the old payload so the allocation is freed then rather than when the pool
 * itself dies.
 */
struct VulkanBuffer
{
    VulkanBuffer() = default;
    VulkanBuffer(VmaAllocator allocator, vk::Buffer buffer, VmaAllocation alloc,
                 const VmaAllocationInfo& info);

    /** move-only, like vk::raii types */
    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    VulkanBuffer(VulkanBuffer&& other) noexcept;
    VulkanBuffer& operator=(VulkanBuffer&& other) noexcept;
    ~VulkanBuffer() { Destroy(); }

    vk::Buffer Buffer{};
    VmaAllocation Allocation{};
    VmaAllocationInfo AllocationInfo{}; // has .pMappedData if persistently mapped

private:
    void Destroy();

    VmaAllocator Allocator{};
};
} // namespace Hikari::Rhi::Vulkan
