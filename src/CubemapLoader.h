#pragma once

#include <memory>

#include <rhi/IDevice.h>
#include <rhi/UploadContext.h>

struct CubemapCreateInfo;

class Cubemap;

class CubemapLoader
{
private:
    friend class ResourceManager;

    CubemapLoader(Hikari::Rhi::IDevice& rhiDevice, Hikari::Rhi::IUploadContext& uploadContext);

    static void Init(Hikari::Rhi::IDevice& rhiDevice, Hikari::Rhi::IUploadContext& uploadContext);
    static void Shutdown();

    static CubemapLoader* Get() { return s_Instance; }

    [[nodiscard]] std::shared_ptr<Cubemap> Load(const CubemapCreateInfo& createInfo);

private:
    inline static CubemapLoader* s_Instance = nullptr;

    Hikari::Rhi::IDevice& m_RhiDevice;
    Hikari::Rhi::IUploadContext& m_UploadContext;
};
