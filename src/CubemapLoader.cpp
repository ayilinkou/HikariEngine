#include "CubemapLoader.h"

#include "stb_image.h"

#include "Cubemap.h"
#include "Log.h"
#include "Utility.h"

constexpr LogCategory LogCubemapLoader("Cubemap Loader");

CubemapLoader::CubemapLoader(vk::raii::Device& device,
                             vk::raii::PhysicalDevice& physicalDevice,
                             vk::raii::CommandPool& commandPool,
                             vk::raii::Queue& transferQueue)
    : m_Device(device), m_PhysicalDevice(physicalDevice),
      m_CommandPool(commandPool), m_TransferQueue(transferQueue)
{
}

void CubemapLoader::Init(vk::raii::Device& device,
                         vk::raii::PhysicalDevice& physicalDevice,
                         vk::raii::CommandPool& commandPool,
                         vk::raii::Queue& transferQueue)
{
    if (s_Instance)
        throw std::runtime_error(
            "CubemapLoader singleton is already initialised!");

    s_Instance =
        new CubemapLoader(device, physicalDevice, commandPool, transferQueue);
}

void CubemapLoader::Shutdown()
{
    if (!s_Instance)
        throw std::runtime_error(
            "Attempting to shutdown CubemapLoader when instance is null!");

    delete s_Instance;
    s_Instance = nullptr;
}

Cubemap* CubemapLoader::Load(const CubemapCreateInfo& createInfo)
{
    struct FaceData
    {
        std::array<stbi_uc*, 6> Pixels;
        int Width, Height, Channels;
    } faceData;

    for (size_t i = 0; i < 6; i++)
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

        LogMsg(LogSeverity::Info, LogCubemapLoader, "Loading texture: {}",
               facePath->c_str());

        faceData.Pixels[i] =
            stbi_load(facePath->c_str(), &faceData.Width, &faceData.Height,
                      &faceData.Channels, STBI_rgb_alpha);

        if (!faceData.Pixels[i])
            throw std::runtime_error(
                std::format("Failed to load texture: {}", facePath->c_str()));
    }

    vk::raii::Buffer stagingBuffer({});
    vk::raii::DeviceMemory stagingMemory({});
    vk::DeviceSize faceSize = faceData.Width * faceData.Height * 4u;
    vk::DeviceSize totalSize = faceSize * 6u;
    CreateBuffer(m_Device, m_PhysicalDevice, totalSize,
                 vk::BufferUsageFlagBits::eTransferSrc,
                 vk::MemoryPropertyFlagBits::eHostVisible |
                     vk::MemoryPropertyFlagBits::eHostCoherent,
                 stagingBuffer, stagingMemory);

    // Vulkan ensures that these CPU writes are visible to the GPU before
    // the command buffer starts executing.
    void* data = stagingMemory.mapMemory(0, totalSize);
    uint8_t* dst = static_cast<uint8_t*>(data);
    for (size_t i = 0; i < 6; i++)
    {
        memcpy(dst + i * faceSize, faceData.Pixels[i], faceSize);
        stbi_image_free(faceData.Pixels[i]);
        faceData.Pixels[i] = nullptr;
    }
    stagingMemory.unmapMemory();

    vk::raii::Image image({});
    vk::raii::DeviceMemory imageMemory({});
    constexpr uint32_t arrayLayers = 6u;
    CreateImage(m_Device, m_PhysicalDevice, faceData.Width, faceData.Height,
                createInfo.Format, vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eTransferDst |
                    vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal, image, imageMemory,
                arrayLayers, vk::ImageCreateFlagBits::eCubeCompatible);
    SetVkDebugName(m_Device, *image, vk::ObjectType::eImage,
                   std::format("{} Cubemap Image", createInfo.Name).c_str());
    SetVkDebugName(
        m_Device, *imageMemory, vk::ObjectType::eDeviceMemory,
        std::format("{} Cubemap Device Memory", createInfo.Name).c_str());

    auto cmd = BeginSingleTimeCommand(m_Device, m_CommandPool);
    TransitionImageLayout(cmd, image, vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eTransferDstOptimal,
                          vk::ImageAspectFlagBits::eColor);
    CopyBufferToImage(cmd, stagingBuffer, image,
                      static_cast<uint32_t>(faceData.Width),
                      static_cast<uint32_t>(faceData.Height));
    TransitionImageLayout(cmd, image, vk::ImageLayout::eTransferDstOptimal,
                          vk::ImageLayout::eShaderReadOnlyOptimal,
                          vk::ImageAspectFlagBits::eColor);
    EndSingleTimeCommand(cmd, m_TransferQueue);

    vk::raii::ImageView imageView = CreateImageView(
        m_Device, image, vk::ImageViewType::eCube, createInfo.Format,
        vk::ImageAspectFlagBits::eColor, arrayLayers);
    SetVkDebugName(
        m_Device, *imageView, vk::ObjectType::eImageView,
        std::format("{} Cubemap Image View", createInfo.Name).c_str());

    return new Cubemap(std::move(image), std::move(imageView),
                       std::move(imageMemory), createInfo);
}
