#include "TextureLoader.h"
#include "AllocatedBuffer.h"
#include "AllocatedImage.h"
#include "Barrier.h"
#include "vulkan/vulkan.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "Log.h"
#include "Utility.h"

constexpr LogCategory LogTextureLoader("Texture Loader");

TextureLoader::TextureLoader(vk::raii::Device& device,
                             vk::raii::PhysicalDevice& physicalDevice,
                             vk::raii::CommandPool& commandPool,
                             vk::raii::Queue& transferQueue,
                             VmaAllocator allocator)
    : m_Device(device), m_PhysicalDevice(physicalDevice),
      m_CommandPool(commandPool), m_TransferQueue(transferQueue),
      m_Allocator(allocator)
{
}

void TextureLoader::Init(vk::raii::Device& device,
                         vk::raii::PhysicalDevice& physicalDevice,
                         vk::raii::CommandPool& commandPool,
                         vk::raii::Queue& transferQueue, VmaAllocator allocator)
{
    if (s_Instance)
        throw std::runtime_error(
            "TextureLoader singleton is already initialised!");

    s_Instance = new TextureLoader(device, physicalDevice, commandPool,
                                   transferQueue, allocator);
}

void TextureLoader::Shutdown()
{
    if (!s_Instance)
        throw std::runtime_error(
            "Attempting to shutdown TextureLoader when instance is null!");

    delete s_Instance;
    s_Instance = nullptr;
}

std::shared_ptr<Texture> TextureLoader::Load(const std::string& path,
                                             const vk::Format format)
{
    LogMsg(LogSeverity::Info, LogTextureLoader, "Loading texture: {}",
           path.c_str());

    int width, height, channels;
    stbi_uc* pixels =
        stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    vk::DeviceSize imageSize = width * height * 4;

    if (!pixels)
    {
        LogMsg(LogSeverity::Error, LogTextureLoader, "Failed to load texture: {}", path.c_str());
        return nullptr;
    }

    std::shared_ptr<Texture> texture =
        CreateTextureFromPixels(pixels, width, height, format, imageSize, path);
    stbi_image_free(pixels);
    return texture;
}

std::shared_ptr<Texture> TextureLoader::CreateTextureFromPixels(
    stbi_uc* pixels, const int width, const int height,
    const vk::Format format, const vk::DeviceSize size, const std::string& path)
{
    AllocatedBuffer stagingBuffer = CreateBuffer(
        m_Allocator, size, vk::BufferUsageFlagBits::eTransferSrc,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT);

    // Vulkan ensures that these CPU writes are visible to the GPU before
    // the command buffer starts executing.
    memcpy(stagingBuffer.AllocationInfo.pMappedData, pixels, size);

    vk::ImageCreateInfo imageInfo{};
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.extent = vk::Extent3D{static_cast<uint32_t>(width),
                                    static_cast<uint32_t>(height), 1u};
    imageInfo.mipLevels = 1u;
    imageInfo.arrayLayers = 1u;
    imageInfo.format = format;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage =
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;

    AllocatedImage image = CreateImage(m_Allocator, imageInfo);
    SetVkDebugName(m_Device, image.Image, vk::ObjectType::eImage,
                   std::format("{} Image", path).c_str());
    vmaSetAllocationName(m_Allocator, image.Allocation,
                         std::format("{} Allocation", path).c_str());

    auto cmd = BeginSingleTimeCommand(m_Device, m_CommandPool);
    RecordImageBarrier(cmd, image, Barriers::UndefinedToTransferDst());
    CopyBufferToImage(cmd, stagingBuffer.Buffer, image.Image,
                      static_cast<uint32_t>(width),
                      static_cast<uint32_t>(height));
    RecordImageBarrier(cmd, image, Barriers::TransferDstToShaderRead());
    EndSingleTimeCommand(cmd, m_TransferQueue);

    vk::raii::ImageView imageView =
        CreateImageView(m_Device, image.Image, vk::ImageViewType::e2D, format,
                        vk::ImageAspectFlagBits::eColor, 1u);
    SetVkDebugName(m_Device, *imageView, vk::ObjectType::eImageView,
                   std::format("{} Image View", path).c_str());

    return std::make_shared<Texture>(std::move(image), std::move(imageView),
                                     path);
}

std::shared_ptr<Texture> TextureLoader::LoadFallbackTexture(const vk::Format format)
{
    LogMsg(LogSeverity::Error, LogTextureLoader, "Loading fallback texture...");
    stbi_uc fallbackPixels[] = {255, 0, 255, 255};
    return CreateTextureFromPixels(fallbackPixels, 1, 1, format, sizeof(fallbackPixels) / sizeof(stbi_uc),
                                   "FallbackTexture");
}