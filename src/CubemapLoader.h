#pragma once

struct CubemapCreateInfo;

class Cubemap;

class CubemapLoader
{
private:
    friend class ResourceManager;

    CubemapLoader(vk::raii::Device& device,
                  vk::raii::PhysicalDevice& physicalDevice,
                  vk::raii::CommandPool& commandPool,
                  vk::raii::Queue& transferQueue);

    static void Init(vk::raii::Device& device,
                     vk::raii::PhysicalDevice& physicalDevice,
                     vk::raii::CommandPool& commandPool,
                     vk::raii::Queue& transferQueue);
    static void Shutdown();

    static CubemapLoader* Get() { return s_Instance; }

    [[nodiscard]] Cubemap* Load(const CubemapCreateInfo& createInfo);

private:
    inline static CubemapLoader* s_Instance = nullptr;

    vk::raii::Device& m_Device;
    vk::raii::PhysicalDevice& m_PhysicalDevice;
    vk::raii::CommandPool& m_CommandPool;
    vk::raii::Queue& m_TransferQueue;
};
