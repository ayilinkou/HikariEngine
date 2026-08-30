#pragma once

#include <memory>
#include <string>

#include <rhi/IDevice.h>
#include <rhi/RhiTypes.h>
#include <rhi/UploadContext.h>

#include "Texture.h"

typedef unsigned char stbi_uc;

class TextureLoader
{
private:
    friend class ResourceManager;

    TextureLoader(Hikari::Rhi::IDevice& rhiDevice, Hikari::Rhi::IUploadContext& uploadContext);

    static void Init(Hikari::Rhi::IDevice& rhiDevice, Hikari::Rhi::IUploadContext& uploadContext);
    static void Shutdown();

    static TextureLoader* Get() { return s_Instance; }

    [[nodiscard]] std::shared_ptr<Texture> Load(const std::string& filepath,
                                                const Hikari::Rhi::Format format);
    [[nodiscard]] std::shared_ptr<Texture> LoadFallbackTexture(const Hikari::Rhi::Format format);
    [[nodiscard]] std::shared_ptr<Texture>
    CreateTextureFromPixels(stbi_uc* pixels, const uint32_t width, const uint32_t height,
                            const Hikari::Rhi::Format format, const uint64_t size,
                            const std::string& name);

private:
    inline static TextureLoader* s_Instance = nullptr;

    Hikari::Rhi::IDevice& m_RhiDevice;
    Hikari::Rhi::IUploadContext& m_UploadContext;
};
