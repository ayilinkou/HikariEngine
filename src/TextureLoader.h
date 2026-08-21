#pragma once

#include <memory>
#include <string>

#include "vulkan/vulkan_raii.hpp"

#include <rhi/IDevice.h>
#include <rhi/RhiTypes.h>

#include "Texture.h"

typedef unsigned char stbi_uc;

class TextureLoader
{
private:
    friend class ResourceManager;

    TextureLoader(Rhi::IDevice& rhiDevice, vk::raii::CommandPool& commandPool,
                  vk::raii::Queue& transferQueue);

    static void Init(Rhi::IDevice& rhiDevice, vk::raii::CommandPool& commandPool,
                     vk::raii::Queue& transferQueue);
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

    // Still Vulkan-shaped because each upload submits on its own and then waits
    // for the queue to drain. An upload context recording many copies behind a
    // single fence is what replaces both of these.
    vk::raii::CommandPool& m_CommandPool;
    vk::raii::Queue& m_TransferQueue;
};
