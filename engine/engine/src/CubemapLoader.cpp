#include "CubemapLoader.h"

#include <array>
#include <span>
#include <stdexcept>
#include <vector>

#include <core/Log.h>

#include "Cubemap.h"

using namespace Hikari;
using namespace Hikari::Core;

constexpr LogCategory LogCubemapLoader("Cubemap Loader");

CubemapLoader::CubemapLoader(Rhi::IDevice& rhiDevice, Rhi::IUploadContext& uploadContext,
                             const Asset::TextureSource& source)
    : m_RhiDevice(rhiDevice), m_UploadContext(uploadContext), m_Source(source)
{
}

std::shared_ptr<Cubemap> CubemapLoader::Load(const std::string& path, const std::string& name)
{
    try
    {
        std::string error;
        std::vector<std::string> warnings;
        const auto data = m_Source.Read(path, error, warnings);
        for (const auto& warning : warnings)
            LogMsg(LogSeverity::Warning, LogCubemapLoader, "{}", warning);
        if (!data)
            throw std::runtime_error(error);
        if (data->Faces != Cubemap::kFaceCount)
            throw std::runtime_error("A cubemap request requires a six-face KTX2 asset");
        if (data->Metadata.Usage && data->Metadata.Usage != Asset::TextureUsage::Color)
            LogMsg(LogSeverity::Warning, LogCubemapLoader,
                   "{}: Cubemap expects color usage; saved usage is {} in {}. Review the settings "
                   "and recook.",
                   path, Asset::ToString(*data->Metadata.Usage),
                   m_Source.SettingsPath(path).string());
        CubemapCreateInfo info;
        info.Name = name;
        info.Format = data->Format;
        auto cubemap = std::make_shared<Cubemap>(
            m_RhiDevice, info,
            Rhi::TextureDesc{.Format = data->Format,
                             .Extent = {data->Extent.Width, data->Extent.Height, 1u},
                             .MipLevels = data->MipLevels,
                             .ArrayLayers = data->Faces,
                             .Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::CopyDst,
                             .bCubeCompatible = true,
                             .DebugName = name},
            path, data->Metadata);
        std::vector<Rhi::TextureUpload> uploads;
        for (const auto& resource : data->Subresources)
            uploads.push_back(
                {.Data = std::span(data->Data).subspan(resource.Offset, resource.Size),
                 .MipLevel = resource.MipLevel,
                 .BaseLayer = resource.Face,
                 .Extent = resource.Extent});
        m_UploadContext.UploadTexture(cubemap->GetHandle(), uploads);
        return cubemap;
    }
    catch (const std::exception& error)
    {
        LogMsg(LogSeverity::Error, LogCubemapLoader, "{}: {}", path, error.what());
        return nullptr;
    }
}

std::shared_ptr<Cubemap> CubemapLoader::LoadFallbackCubemap()
{
    CubemapCreateInfo info;
    info.Name = "FallbackCubemap";
    info.Format = Rhi::Format::RGBA8Srgb;
    auto cubemap = std::make_shared<Cubemap>(m_RhiDevice, info, Core::Extent2D{1u, 1u});
    const std::array<uint8_t, 4> pixel{255u, 0u, 255u, 255u};
    std::array<Rhi::TextureUpload, Cubemap::kFaceCount> uploads;
    for (uint32_t face = 0; face < uploads.size(); ++face)
        uploads[face] = {
            .Data = std::as_bytes(std::span(pixel)), .BaseLayer = face, .Extent = {1u, 1u, 1u}};
    m_UploadContext.UploadTexture(cubemap->GetHandle(), uploads);
    return cubemap;
}
