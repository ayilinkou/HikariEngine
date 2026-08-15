#pragma once

#include "vk_mem_alloc.h"
#include "vulkan/vulkan_raii.hpp"

#include "Texture.h"

typedef unsigned char stbi_uc;

class TextureLoader
{
private:
    friend class ResourceManager;

    TextureLoader(vk::raii::Device& device, vk::raii::PhysicalDevice& physicalDevice,
                  vk::raii::CommandPool& commandPool, vk::raii::Queue& transferQueue,
                  VmaAllocator allocator);

    static void Init(vk::raii::Device& device, vk::raii::PhysicalDevice& physicalDevice,
                     vk::raii::CommandPool& commandPool, vk::raii::Queue& transferQueue,
                     VmaAllocator allocator);
    static void Shutdown();

    static TextureLoader* Get() { return s_Instance; }

    [[nodiscard]] std::shared_ptr<Texture> Load(const std::string& filepath,
                                                const vk::Format format);
    [[nodiscard]] std::shared_ptr<Texture> LoadFallbackTexture(const vk::Format format);
    [[nodiscard]] std::shared_ptr<Texture> CreateTextureFromPixels(stbi_uc* pixels, const int width,
                                                                   const int height,
                                                                   const vk::Format format,
                                                                   const vk::DeviceSize size,
                                                                   const std::string& name);

private:
    inline static TextureLoader* s_Instance = nullptr;

    vk::raii::Device& m_Device;
    vk::raii::PhysicalDevice& m_PhysicalDevice;
    vk::raii::CommandPool& m_CommandPool;
    vk::raii::Queue& m_TransferQueue;

    VmaAllocator m_Allocator;
};
