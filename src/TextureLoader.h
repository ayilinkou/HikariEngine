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

    TextureLoader(Rhi::IDevice& rhiDevice, Rhi::IUploadContext& uploadContext);

    static void Init(Rhi::IDevice& rhiDevice, Rhi::IUploadContext& uploadContext);
    static void Shutdown();

    static TextureLoader* Get() { return s_Instance; }

    [[nodiscard]] std::shared_ptr<Texture> Load(const std::string& filepath,
                                                const Rhi::Format format);
    [[nodiscard]] std::shared_ptr<Texture> LoadFallbackTexture(const Rhi::Format format);
    [[nodiscard]] std::shared_ptr<Texture>
    CreateTextureFromPixels(stbi_uc* pixels, const uint32_t width, const uint32_t height,
                            const Rhi::Format format, const uint64_t size, const std::string& name);

private:
    inline static TextureLoader* s_Instance = nullptr;

    Rhi::IDevice& m_RhiDevice;
    Rhi::IUploadContext& m_UploadContext;
};
