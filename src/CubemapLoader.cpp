#include "CubemapLoader.h"

#include "Cubemap.h"
#include "ResourceManager.h"
#include "stb_image.h"

#include "vulkan/vulkan.hpp"
#include <core/Log.h>

#include <rhi/BarrierPresets.h>
#include <rhi/ICommandList.h>
#include <rhi/UniqueHandle.h>
#include <rhi/vulkan/CommandListUtil.h>
#include <rhi/vulkan/VulkanNative.h>

constexpr LogCategory LogCubemapLoader("Cubemap Loader");

CubemapLoader::CubemapLoader(Rhi::IDevice& rhiDevice, vk::raii::CommandPool& commandPool,
                             vk::raii::Queue& transferQueue)
    : m_RhiDevice(rhiDevice), m_CommandPool(commandPool), m_TransferQueue(transferQueue)
{
}

void CubemapLoader::Init(Rhi::IDevice& rhiDevice, vk::raii::CommandPool& commandPool,
                         vk::raii::Queue& transferQueue)
{
    if (s_Instance)
        throw std::runtime_error("CubemapLoader singleton is already initialised!");

    s_Instance = new CubemapLoader(rhiDevice, commandPool, transferQueue);
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
    static constexpr uint32_t faceCount = Cubemap::kFaceCount;
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

    const uint32_t width = static_cast<uint32_t>(faceData.Width);
    const uint32_t height = static_cast<uint32_t>(faceData.Height);
    const uint64_t faceSize = static_cast<uint64_t>(width) * height * 4u;
    const uint64_t totalSize = faceSize * faceCount;

    Rhi::UniqueHandle<Rhi::BufferHandle> stagingBuffer(
        m_RhiDevice, m_RhiDevice.CreateBuffer(Rhi::BufferDesc{
                         .Size = totalSize,
                         .Usage = Rhi::BufferUsage::CopySrc,
                         .Access = Rhi::MemoryAccess::CpuToGpu,
                         .DebugName = std::format("{} Cubemap Staging", createInfo.Name)}));

    // Vulkan ensures that these CPU writes are visible to the GPU before
    // the command buffer starts executing.
    uint8_t* dst = static_cast<uint8_t*>(m_RhiDevice.GetMappedData(stagingBuffer.Get()));
    for (size_t i = 0; i < faceCount; i++)
    {
        memcpy(dst + i * faceSize, faceData.Pixels[i], faceSize);
        stbi_image_free(faceData.Pixels[i]);
        faceData.Pixels[i] = nullptr;
    }

    auto cubemap = std::make_shared<Cubemap>(m_RhiDevice, createInfo, Rhi::Extent2D{width, height});

    vk::raii::CommandBuffer cmd =
        BeginSingleTimeCommand(Rhi::Vulkan::GetDevice(m_RhiDevice), m_CommandPool);
    std::unique_ptr<Rhi::ICommandList> list = Rhi::Vulkan::WrapCommandList(m_RhiDevice, *cmd);

    list->Barrier(Rhi::BarrierPresets::UndefinedToCopyDst(faceCount).On(cubemap->GetHandle()));
    list->CopyBufferToTexture(
        stagingBuffer.Get(), cubemap->GetHandle(),
        Rhi::BufferTextureCopyRegion{.LayerCount = faceCount, .Extent = {width, height, 1u}});
    list->Barrier(Rhi::BarrierPresets::CopyDstToShaderResource(faceCount).On(cubemap->GetHandle()));

    EndSingleTimeCommand(cmd, m_TransferQueue);

    return cubemap;
}
