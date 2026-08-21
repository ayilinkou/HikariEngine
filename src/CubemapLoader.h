#pragma once

#include <memory>

#include "vulkan/vulkan_raii.hpp"

#include <rhi/IDevice.h>

struct CubemapCreateInfo;

class Cubemap;

class CubemapLoader
{
private:
    friend class ResourceManager;

    CubemapLoader(Rhi::IDevice& rhiDevice, vk::raii::CommandPool& commandPool,
                  vk::raii::Queue& transferQueue);

    static void Init(Rhi::IDevice& rhiDevice, vk::raii::CommandPool& commandPool,
                     vk::raii::Queue& transferQueue);
    static void Shutdown();

    static CubemapLoader* Get() { return s_Instance; }

    [[nodiscard]] std::shared_ptr<Cubemap> Load(const CubemapCreateInfo& createInfo);

private:
    inline static CubemapLoader* s_Instance = nullptr;

    Rhi::IDevice& m_RhiDevice;

    // Still Vulkan-shaped because each upload submits on its own and then waits
    // for the queue to drain. An upload context recording many copies behind a
    // single fence is what replaces both of these.
    vk::raii::CommandPool& m_CommandPool;
    vk::raii::Queue& m_TransferQueue;
};
