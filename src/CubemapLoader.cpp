#include "CubemapLoader.h"

#include "ResourceManager.h"
#include "stb_image.h"

#include "Utility.h"
#include "vulkan/vulkan.hpp"
#include <core/Log.h>

#include <rhi/vulkan/Barrier.h>
#include <rhi/vulkan/Cubemap.h>

constexpr LogCategory LogCubemapLoader("Cubemap Loader");

CubemapLoader::CubemapLoader(vk::raii::Device& device, vk::raii::PhysicalDevice& physicalDevice,
                             vk::raii::CommandPool& commandPool, vk::raii::Queue& transferQueue,
                             VmaAllocator allocator)
    : m_Device(device), m_PhysicalDevice(physicalDevice), m_CommandPool(commandPool),
      m_TransferQueue(transferQueue), m_Allocator(allocator)
{
}

void CubemapLoader::Init(vk::raii::Device& device, vk::raii::PhysicalDevice& physicalDevice,
                         vk::raii::CommandPool& commandPool, vk::raii::Queue& transferQueue,
                         VmaAllocator allocator)
{
    if (s_Instance)
        throw std::runtime_error("CubemapLoader singleton is already initialised!");

    s_Instance = new CubemapLoader(device, physicalDevice, commandPool, transferQueue, allocator);
}

void CubemapLoader::Shutdown()
{
    if (!s_Instance)
        throw std::runtime_error("Attempting to shutdown CubemapLoader when instance is null!");

    delete s_Instance;
    s_Instance = nullptr;
}

std::shared_ptr<Cubemap> CubemapLoader::Load(const CubemapCreateInfo& createInfo)
{
    static constexpr uint32_t faceCount = 6u;
    struct FaceData
    {
        std::array<stbi_uc*, faceCount> Pixels;
        int Width, Height, Channels;
    } faceData;

    for (size_t i = 0; i < faceCount; i++)
    {
        const std::string* facePath;

        // face order: +X, -X, +Y, -Y, +Z, -Z
        switch (i)
        {
            case 0:
                facePath = &createInfo.RightPath;
                break;
            case 1:
                facePath = &createInfo.LeftPath;
                break;
            case 2:
                facePath = &createInfo.TopPath;
                break;
            case 3:
                facePath = &createInfo.BottomPath;
                break;
            case 4:
                facePath = &createInfo.BackPath;
                break;
            case 5:
                facePath = &createInfo.FrontPath;
                break;
            default:
                throw std::runtime_error("Cubemap has only 6 faces!");
        }

        LogMsg(LogSeverity::Info, LogCubemapLoader, "Loading texture: {}", facePath->c_str());

        faceData.Pixels[i] = stbi_load(facePath->c_str(), &faceData.Width, &faceData.Height,
                                       &faceData.Channels, STBI_rgb_alpha);

        if (!faceData.Pixels[i])
            throw std::runtime_error(std::format("Failed to load texture: {}", facePath->c_str()));
    }

    vk::DeviceSize faceSize = faceData.Width * faceData.Height * 4u;
    vk::DeviceSize totalSize = faceSize * faceCount;
    // TODO: fix and make use VMA
    AllocatedBuffer stagingBuffer = CreateBuffer(
        m_Allocator, totalSize, vk::BufferUsageFlagBits::eTransferSrc,
        VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

    // Vulkan ensures that these CPU writes are visible to the GPU before
    // the command buffer starts executing.
    uint8_t* dst = static_cast<uint8_t*>(stagingBuffer.AllocationInfo.pMappedData);
    for (size_t i = 0; i < faceCount; i++)
    {
        memcpy(dst + i * faceSize, faceData.Pixels[i], faceSize);
        stbi_image_free(faceData.Pixels[i]);
        faceData.Pixels[i] = nullptr;
    }

    vk::ImageCreateInfo imageInfo{};
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.extent = vk::Extent3D{static_cast<uint32_t>(faceData.Width),
                                    static_cast<uint32_t>(faceData.Height), 1};
    imageInfo.mipLevels = 1u;
    imageInfo.arrayLayers = faceCount;
    imageInfo.format = createInfo.Format;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;
    imageInfo.flags = vk::ImageCreateFlagBits::eCubeCompatible;

    AllocatedImage cubemapImage = CreateImage(m_Allocator, imageInfo);
    SetVkDebugName(m_Device, cubemapImage.Image, vk::ObjectType::eImage,
                   std::format("{} Cubemap Image", createInfo.Name).c_str());
    vmaSetAllocationName(m_Allocator, cubemapImage.Allocation,
                         std::format("{} Cubemap Device allocation", createInfo.Name).c_str());

    auto cmd = BeginSingleTimeCommand(m_Device, m_CommandPool);
    RecordImageBarrier(cmd, cubemapImage.Image, Barriers::UndefinedToTransferDst(faceCount));
    CopyBufferToImage(cmd, stagingBuffer.Buffer, cubemapImage.Image,
                      static_cast<uint32_t>(faceData.Width), static_cast<uint32_t>(faceData.Height),
                      faceCount);
    RecordImageBarrier(cmd, cubemapImage.Image, Barriers::TransferDstToShaderRead(faceCount));
    EndSingleTimeCommand(cmd, m_TransferQueue);

    vk::raii::ImageView imageView =
        CreateImageView(m_Device, cubemapImage.Image, vk::ImageViewType::eCube, createInfo.Format,
                        vk::ImageAspectFlagBits::eColor, faceCount);
    SetVkDebugName(m_Device, *imageView, vk::ObjectType::eImageView,
                   std::format("{} Cubemap Image View", createInfo.Name).c_str());

    return std::make_shared<Cubemap>(std::move(cubemapImage), std::move(imageView), createInfo);
}
