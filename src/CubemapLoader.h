#pragma once

#include <memory>

#include "vk_mem_alloc.h"
#include "vulkan/vulkan_raii.hpp"

#include <rhi/IDevice.h>

struct CubemapCreateInfo;

class Cubemap;

class CubemapLoader
{
private:
    friend class ResourceManager;

    CubemapLoader(Rhi::IDevice& rhiDevice, vk::raii::Device& device,
                  vk::raii::PhysicalDevice& physicalDevice, vk::raii::CommandPool& commandPool,
                  vk::raii::Queue& transferQueue, VmaAllocator allocator);

    static void Init(Rhi::IDevice& rhiDevice, vk::raii::Device& device,
                     vk::raii::PhysicalDevice& physicalDevice, vk::raii::CommandPool& commandPool,
                     vk::raii::Queue& transferQueue, VmaAllocator allocator);
    static void Shutdown();

    static CubemapLoader* Get() { return s_Instance; }

    [[nodiscard]] std::shared_ptr<Cubemap> Load(const CubemapCreateInfo& createInfo);

private:
    inline static CubemapLoader* s_Instance = nullptr;

    Rhi::IDevice& m_RhiDevice;
    vk::raii::Device& m_Device;
    vk::raii::PhysicalDevice& m_PhysicalDevice;
    vk::raii::CommandPool& m_CommandPool;
    vk::raii::Queue& m_TransferQueue;

    VmaAllocator m_Allocator{};
};
