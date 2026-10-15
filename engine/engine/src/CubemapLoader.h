#pragma once

#include <asset/TextureSource.h>
#include <memory>
#include <string>

#include <rhi/IDevice.h>
#include <rhi/UploadContext.h>

struct CubemapCreateInfo;

class Cubemap;

/** Owned by the AssetRegistry that loads through it; it caches nothing itself. */
class CubemapLoader
{
public:
    CubemapLoader(Hikari::Rhi::IDevice& rhiDevice, Hikari::Rhi::IUploadContext& uploadContext,
                  const Hikari::Asset::TextureSource& source);

    [[nodiscard]] std::shared_ptr<Cubemap> Load(const std::string& path, const std::string& name);
    [[nodiscard]] std::shared_ptr<Cubemap> LoadFallbackCubemap();

private:
    Hikari::Rhi::IDevice& m_RhiDevice;
    Hikari::Rhi::IUploadContext& m_UploadContext;
    const Hikari::Asset::TextureSource& m_Source;
};
