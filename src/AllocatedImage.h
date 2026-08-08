#pragma once

#include "vk_mem_alloc.h"

struct AllocatedImage
{
    AllocatedImage() = default;
    AllocatedImage(VmaAllocator allocator, vk::Image image,
                   VmaAllocation alloc);
    AllocatedImage(const AllocatedImage&) = delete;
    AllocatedImage& operator=(const AllocatedImage&) = delete;
    AllocatedImage(AllocatedImage&& other) noexcept;
    AllocatedImage& operator=(AllocatedImage&& other) noexcept;
    ~AllocatedImage() { Destroy(); }

	vk::Image Image{};
    VmaAllocation Allocation{};

private:
    void Destroy();

    VmaAllocator Allocator{};
};
