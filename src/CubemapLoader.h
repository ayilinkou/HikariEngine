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

    CubemapLoader(Rhi::IDevice& rhiDevice, Rhi::IUploadContext& uploadContext);

    static void Init(Rhi::IDevice& rhiDevice, Rhi::IUploadContext& uploadContext);
    static void Shutdown();

    static CubemapLoader* Get() { return s_Instance; }

    [[nodiscard]] std::shared_ptr<Cubemap> Load(const CubemapCreateInfo& createInfo);

private:
    inline static CubemapLoader* s_Instance = nullptr;

    Rhi::IDevice& m_RhiDevice;
    Rhi::IUploadContext& m_UploadContext;
};
