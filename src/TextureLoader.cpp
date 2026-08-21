#include "TextureLoader.h"

#include "vulkan/vulkan.hpp"
#include <rhi/BarrierPresets.h>
#include <rhi/ICommandList.h>
#include <rhi/UniqueHandle.h>
#include <rhi/vulkan/CommandListUtil.h>
#include <rhi/vulkan/VulkanNative.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <core/Log.h>

constexpr LogCategory LogTextureLoader("Texture Loader");

TextureLoader::TextureLoader(Rhi::IDevice& rhiDevice, vk::raii::CommandPool& commandPool,
                             vk::raii::Queue& transferQueue)
    : m_RhiDevice(rhiDevice), m_CommandPool(commandPool), m_TransferQueue(transferQueue)
{
}

void TextureLoader::Init(Rhi::IDevice& rhiDevice, vk::raii::CommandPool& commandPool,
                         vk::raii::Queue& transferQueue)
{
    if (s_Instance)
        throw std::runtime_error("TextureLoader singleton is already initialised!");

    s_Instance = new TextureLoader(rhiDevice, commandPool, transferQueue);
}

void TextureLoader::Shutdown()
{
    if (!s_Instance)
        throw std::runtime_error("Attempting to shutdown TextureLoader when instance is null!");

    delete s_Instance;
    s_Instance = nullptr;
}

std::shared_ptr<Texture> TextureLoader::Load(const std::string& path, const Rhi::Format format)
{
    LogMsg(LogSeverity::Info, LogTextureLoader, "Loading texture: {}", path.c_str());

    int width, height, channels;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels)
    {
        LogMsg(LogSeverity::Error, LogTextureLoader, "Failed to load texture: {}", path.c_str());
        return nullptr;
    }

    const uint64_t imageSize = static_cast<uint64_t>(width) * height * 4u;
    std::shared_ptr<Texture> texture =
        CreateTextureFromPixels(pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                                format, imageSize, path);
    stbi_image_free(pixels);
    return texture;
}

std::shared_ptr<Texture>
TextureLoader::CreateTextureFromPixels(stbi_uc* pixels, const uint32_t width, const uint32_t height,
                                       const Rhi::Format format, const uint64_t size,
                                       const std::string& path)
{
    Rhi::UniqueHandle<Rhi::BufferHandle> stagingBuffer(
        m_RhiDevice,
        m_RhiDevice.CreateBuffer(Rhi::BufferDesc{.Size = size,
                                                 .Usage = Rhi::BufferUsage::CopySrc,
                                                 .Access = Rhi::MemoryAccess::CpuToGpu,
                                                 .DebugName = std::format("{} Staging", path)}));

    // Vulkan ensures that these CPU writes are visible to the GPU before
    // the command buffer starts executing.
    memcpy(m_RhiDevice.GetMappedData(stagingBuffer.Get()), pixels, size);

    auto texture = std::make_shared<Texture>(
        m_RhiDevice,
        Rhi::TextureDesc{.Format = format,
                         .Extent = {width, height, 1u},
                         .Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::CopyDst,
                         .DebugName = path},
        Rhi::TextureViewDimension::Texture2D, path);

    vk::raii::CommandBuffer cmd =
        BeginSingleTimeCommand(Rhi::Vulkan::GetDevice(m_RhiDevice), m_CommandPool);
    std::unique_ptr<Rhi::ICommandList> list = Rhi::Vulkan::WrapCommandList(m_RhiDevice, *cmd);

    list->Barrier(Rhi::BarrierPresets::UndefinedToCopyDst().On(texture->GetHandle()));
    list->CopyBufferToTexture(stagingBuffer.Get(), texture->GetHandle(),
                              Rhi::BufferTextureCopyRegion{.Extent = {width, height, 1u}});
    list->Barrier(Rhi::BarrierPresets::CopyDstToShaderResource().On(texture->GetHandle()));

    EndSingleTimeCommand(cmd, m_TransferQueue);

    return texture;
}

std::shared_ptr<Texture> TextureLoader::LoadFallbackTexture(const Rhi::Format format)
{
    LogMsg(LogSeverity::Error, LogTextureLoader, "Loading fallback texture...");
    stbi_uc fallbackPixels[] = {255, 0, 255, 255};
    return CreateTextureFromPixels(fallbackPixels, 1u, 1u, format,
                                   sizeof(fallbackPixels) / sizeof(stbi_uc), "FallbackTexture");
}
