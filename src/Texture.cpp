#include <iostream>
#include <format>

#include "Texture.h"
#include "Utility.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

void Texture::LoadTexture(vk::raii::Device& device,
                          vk::raii::PhysicalDevice& physicalDevice,
                          vk::raii::CommandPool& commandPool,
                          vk::raii::Queue& transferQueue,
                          const std::string& path)
{
	std::cout << std::format("Loading texture: {}", path.c_str()) << "\n";
	
    m_Name = path;
    CreateTextureImage(device, physicalDevice, commandPool, transferQueue,
                       path);
}

void Texture::CreateTextureImage(vk::raii::Device& device,
                                 vk::raii::PhysicalDevice& physicalDevice,
                                 vk::raii::CommandPool& commandPool,
                                 vk::raii::Queue& transferQueue,
                                 const std::string& path)
{
    int width, height, channels;
    stbi_uc* pixels =
        stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    vk::DeviceSize imageSize = width * height * 4;

    if (!pixels)
        throw std::runtime_error(
            std::format("Failed to load texture: {}", path.c_str()));

    vk::raii::Buffer stagingBuffer({});
    vk::raii::DeviceMemory stagingMemory({});
    CreateBuffer(device, physicalDevice, imageSize,
                 vk::BufferUsageFlagBits::eTransferSrc,
                 vk::MemoryPropertyFlagBits::eHostVisible |
                     vk::MemoryPropertyFlagBits::eHostCoherent,
                 stagingBuffer, stagingMemory);

    // Vulkan ensures that these CPU writes are visible to the GPU before
    // the command buffer starts executing.
    void* data = stagingMemory.mapMemory(0, imageSize);
    memcpy(data, pixels, imageSize);
    stagingMemory.unmapMemory();

    stbi_image_free(pixels);

    CreateImage(
        device, physicalDevice, width, height, vk::Format::eR8G8B8A8Srgb,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        vk::MemoryPropertyFlagBits::eDeviceLocal, m_Image, m_ImageMemory);

    TransitionImageLayout(device, commandPool, transferQueue, m_Image,
                          vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eTransferDstOptimal,
                          vk::ImageAspectFlagBits::eColor);
    CopyBufferToImage(device, commandPool, transferQueue, stagingBuffer,
                      m_Image, static_cast<uint32_t>(width),
                      static_cast<uint32_t>(height));
    TransitionImageLayout(device, commandPool, transferQueue, m_Image,
                          vk::ImageLayout::eTransferDstOptimal,
                          vk::ImageLayout::eShaderReadOnlyOptimal,
                          vk::ImageAspectFlagBits::eColor);

    m_ImageView = CreateImageView(device, m_Image, vk::Format::eR8G8B8A8Srgb,
                                  vk::ImageAspectFlagBits::eColor);
}

