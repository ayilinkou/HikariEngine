#pragma once

#include <utility>

#include "vk_mem_alloc.h"
#include "vulkan/vulkan.hpp"

#include <rhi/TextureDesc.h>

namespace Hikari::Rhi::Vulkan
{
// What a TextureHandle resolves to: the image, its VMA allocation, and the
// description it was created from.
//
// Module-private, and the payload of the device's Core::HandlePool — the same
// arrangement as VulkanBuffer, and for the same reasons (plan D2).
//
// A null Allocation means the device did not allocate the image and must not
// free it. That is how a swapchain image is held: the presentation engine owns
// it, but everything that names a texture — a barrier, a view, a copy — needs
// one identity to name it by, so it gets a pool slot like any other texture and
// releasing that slot frees nothing.
//
// The description is kept because a texture's extent, format and layer count
// are needed wherever it is used, and the alternative is every caller keeping
// its own copy in step with the real one.
struct VulkanTexture
{
    VulkanTexture() = default;
    VulkanTexture(VmaAllocator allocator, vk::Image image, VmaAllocation alloc, TextureDesc desc);

    // move-only, like vk::raii types
    VulkanTexture(const VulkanTexture&) = delete;
    VulkanTexture& operator=(const VulkanTexture&) = delete;
    VulkanTexture(VulkanTexture&& other) noexcept;
    VulkanTexture& operator=(VulkanTexture&& other) noexcept;
    ~VulkanTexture() { Destroy(); }

    vk::Image Image{};
    VmaAllocation Allocation{}; // null when the device does not own Image
    TextureDesc Desc{};

private:
    void Destroy();

    VmaAllocator Allocator{};
};
} // namespace Hikari::Rhi::Vulkan
