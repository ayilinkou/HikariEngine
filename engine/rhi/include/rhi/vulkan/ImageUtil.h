#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

#include "vk_mem_alloc.h"
#include "vulkan/vulkan_raii.hpp"

#include <rhi/vulkan/AllocatedImage.h>
#include <rhi/vulkan/DebugNames.h>
#include <rhi/vulkan/Texture.h>

// VMA-backed image creation, views, buffer-to-image copies, and the
// render-target convenience wrapper.
//
// CreateRenderTexture returns a Texture, which is really an asset-layer type (a
// cache key plus a GPU image) that currently still lives in this module. When
// Texture moves out, this function either follows it or grows a Texture-free
// counterpart — the rest of this header has no such entanglement.

[[nodiscard]] inline vk::raii::ImageView
CreateImageView(vk::raii::Device& device, const vk::Image& image, vk::ImageViewType imageViewType,
                vk::Format format, vk::ImageAspectFlags aspectFlags, uint32_t layerCount)
{
    vk::ImageViewCreateInfo createInfo{.image = image,
                                       .viewType = imageViewType,
                                       .format = format,
                                       .subresourceRange = {.aspectMask = aspectFlags,
                                                            .baseMipLevel = 0u,
                                                            .levelCount = 1u,
                                                            .baseArrayLayer = 0u,
                                                            .layerCount = layerCount}};
    return vk::raii::ImageView(device, createInfo);
}

[[nodiscard]] inline AllocatedImage CreateImage(VmaAllocator allocator,
                                                vk::ImageCreateInfo imageInfo)
{
    VkImageCreateInfo cImageInfo = static_cast<VkImageCreateInfo>(imageInfo);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    VkImage rawImage;
    VmaAllocation allocation;

    vk::Result result = static_cast<vk::Result>(
        vmaCreateImage(allocator, &cImageInfo, &allocInfo, &rawImage, &allocation, nullptr));

    if (result != vk::Result::eSuccess)
        throw std::runtime_error("Failed to create image via VMA!");

    return AllocatedImage(allocator, vk::Image(rawImage), allocation);
}

inline void CopyBufferToImage(vk::raii::CommandBuffer& cmd, vk::Buffer buffer, vk::Image image,
                              uint32_t width, uint32_t height, uint32_t layerCount = 1u)
{
    vk::BufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, layerCount},
        .imageOffset = {0, 0, 0},
        .imageExtent = {width, height, 1}};
    cmd.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, {region});
}

[[nodiscard]] inline Texture CreateRenderTexture(VmaAllocator allocator, vk::raii::Device& device,
                                                 uint32_t width, uint32_t height, vk::Format format,
                                                 vk::ImageUsageFlags usage,
                                                 vk::ImageAspectFlags aspect,
                                                 const std::string& name)
{
    vk::ImageCreateInfo imageInfo{};
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.extent = vk::Extent3D{width, height, 1};
    imageInfo.mipLevels = 1u;
    imageInfo.arrayLayers = 1u;
    imageInfo.format = format;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = usage;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;

    AllocatedImage image = CreateImage(allocator, imageInfo);
    vk::raii::ImageView imageView =
        CreateImageView(device, image.Image, vk::ImageViewType::e2D, format, aspect, 1u);

    SetVkDebugName(device, image.Image, vk::ObjectType::eImage, name.c_str());
    SetVkDebugName(device, *imageView, vk::ObjectType::eImageView, (name + " View").c_str());
    vmaSetAllocationName(allocator, image.Allocation, (name + " allocation").c_str());
    Texture tex(std::move(image), std::move(imageView), name.c_str());
    return tex;
}
