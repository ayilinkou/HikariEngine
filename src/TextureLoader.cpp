#include "TextureLoader.h"

#include <stdexcept>
#include <iostream>
#include <format>

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#include "Texture.h"
#include "Utility.h"

TextureLoader::TextureLoader(vk::raii::Device& device,
                             vk::raii::PhysicalDevice& physicalDevice,
                             vk::raii::CommandPool& commandPool,
                             vk::raii::Queue& transferQueue)
    : m_Device(device), m_PhysicalDevice(physicalDevice),
      m_CommandPool(commandPool), m_TransferQueue(transferQueue)
{
}

void TextureLoader::Init(vk::raii::Device& device,
                         vk::raii::PhysicalDevice& physicalDevice,
                         vk::raii::CommandPool& commandPool,
                         vk::raii::Queue& transferQueue)
{
    if (s_Instance)
        throw std::runtime_error(
            "TextureLoader singleton is already initialised!");

    s_Instance =
        new TextureLoader(device, physicalDevice, commandPool, transferQueue);
}

void TextureLoader::Shutdown()
{
    if (!s_Instance)
        throw std::runtime_error(
            "Attempting to shutdown TextureLoader when instance is null!");

    delete s_Instance;
    s_Instance = nullptr;
}

Texture* TextureLoader::Load(const std::string& path, const vk::Format format)
{
    std::cout << std::format("Loading texture: {}", path.c_str()) << "\n";

	int width, height, channels;
    stbi_uc* pixels =
        stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    vk::DeviceSize imageSize = width * height * 4;

    if (!pixels)
        throw std::runtime_error(
            std::format("Failed to load texture: {}", path.c_str()));

    vk::raii::Buffer stagingBuffer({});
    vk::raii::DeviceMemory stagingMemory({});
    CreateBuffer(m_Device, m_PhysicalDevice, imageSize,
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

    vk::raii::Image image({});
    vk::raii::DeviceMemory imageMemory({});
    CreateImage(m_Device, m_PhysicalDevice, width, height, format,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eTransferDst |
                    vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal, image, imageMemory);

    TransitionImageLayout(m_Device, m_CommandPool, m_TransferQueue, image,
                          vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eTransferDstOptimal,
                          vk::ImageAspectFlagBits::eColor);
    CopyBufferToImage(m_Device, m_CommandPool, m_TransferQueue, stagingBuffer,
                      image, static_cast<uint32_t>(width),
                      static_cast<uint32_t>(height));
    TransitionImageLayout(m_Device, m_CommandPool, m_TransferQueue, image,
                          vk::ImageLayout::eTransferDstOptimal,
                          vk::ImageLayout::eShaderReadOnlyOptimal,
                          vk::ImageAspectFlagBits::eColor);

    vk::raii::ImageView imageView = CreateImageView(
        m_Device, image, format, vk::ImageAspectFlagBits::eColor);

    return new Texture(std::move(image), std::move(imageView),
                       std::move(imageMemory), path);
}
