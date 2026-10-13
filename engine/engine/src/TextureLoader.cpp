#include "TextureLoader.h"

#include <span>

#include <core/Log.h>

using namespace Hikari;
using namespace Hikari::Core;

constexpr LogCategory LogTextureLoader("Texture Loader");

TextureLoader::TextureLoader(Rhi::IDevice& rhiDevice, Rhi::IUploadContext& uploadContext,
                             const Asset::TextureSource& source)
    : m_RhiDevice(rhiDevice), m_UploadContext(uploadContext), m_Source(source)
{
}

std::shared_ptr<Texture> TextureLoader::Load(const std::string& path)
{
    LogMsg(LogSeverity::Info, LogTextureLoader, "Loading texture: {}", path.c_str());

    try
    {
        std::string error;
        std::vector<std::string> warnings;
        const auto data = m_Source.Read(path, error, warnings);
        for (const auto& warning : warnings)
            LogMsg(LogSeverity::Warning, LogTextureLoader, "{}", warning);
        if (!data)
            throw std::runtime_error(error);
        if (data->Faces != 1)
            throw std::runtime_error("A 2D texture request cannot load a cubemap");
        auto texture = std::make_shared<Texture>(
            m_RhiDevice,
            Rhi::TextureDesc{.Format = data->Format,
                             .Extent = {data->Extent.Width, data->Extent.Height, 1u},
                             .MipLevels = data->MipLevels,
                             .Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::CopyDst,
                             .DebugName = path},
            Rhi::TextureViewDimension::Texture2D, path, data->Metadata,
            m_Source.SettingsPath(path).string());
        std::vector<Rhi::TextureUpload> uploads;
        for (const auto& resource : data->Subresources)
            uploads.push_back(
                {.Data = std::span(data->Data).subspan(resource.Offset, resource.Size),
                 .MipLevel = resource.MipLevel,
                 .BaseLayer = resource.Face,
                 .Extent = resource.Extent});
        m_UploadContext.UploadTexture(texture->GetHandle(), uploads);
        return texture;
    }
    catch (const std::exception& error)
    {
        LogMsg(LogSeverity::Error, LogTextureLoader, "{}: {}", path, error.what());
        return nullptr;
    }
}

std::shared_ptr<Texture>
TextureLoader::CreateTextureFromPixels(stbi_uc* pixels, const uint32_t width, const uint32_t height,
                                       const Rhi::Format format, const uint64_t size,
                                       const std::string& path)
{
    auto texture = std::make_shared<Texture>(
        m_RhiDevice,
        Rhi::TextureDesc{.Format = format,
                         .Extent = {width, height, 1u},
                         .Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::CopyDst,
                         .DebugName = path},
        Rhi::TextureViewDimension::Texture2D, path);

    // The context copies the pixels into staging before returning, so the
    // caller's stbi buffer can be freed as soon as this call is done. The
    // texture itself only holds them once a flush covering it has returned,
    // which the AssetRegistry does before handing the resource back.
    m_UploadContext.UploadTexture(
        texture->GetHandle(),
        Rhi::TextureUpload{.Data = std::span(reinterpret_cast<const std::byte*>(pixels), size),
                           .Extent = {width, height, 1u}});

    return texture;
}

std::shared_ptr<Texture> TextureLoader::LoadFallbackTexture(const Rhi::Format format)
{
    LogMsg(LogSeverity::Error, LogTextureLoader, "Loading fallback texture...");
    stbi_uc fallbackPixels[] = {255, 0, 255, 255};
    return CreateTextureFromPixels(fallbackPixels, 1u, 1u, format,
                                   sizeof(fallbackPixels) / sizeof(stbi_uc), "FallbackTexture");
}
