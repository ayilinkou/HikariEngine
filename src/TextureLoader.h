#pragma once

#include <string>

#include "vulkan/vulkan_raii.hpp"

class Texture;

class TextureLoader
{
private:
	friend class ResourceManager;
   
	TextureLoader(vk::raii::Device& device,
                  vk::raii::PhysicalDevice& physicalDevice,
                  vk::raii::CommandPool& commandPool,
                  vk::raii::Queue& transferQueue);

    static void Init(vk::raii::Device& device,
                     vk::raii::PhysicalDevice& physicalDevice,
                     vk::raii::CommandPool& commandPool,
                     vk::raii::Queue& transferQueue);
    static void Shutdown();

    static TextureLoader* Get() { return s_Instance; }

    [[nodiscard]] Texture* Load(const std::string& filepath,
                                       const vk::Format format);

private:
    inline static TextureLoader* s_Instance = nullptr;

    vk::raii::Device& m_Device;
    vk::raii::PhysicalDevice& m_PhysicalDevice;
    vk::raii::CommandPool& m_CommandPool;
    vk::raii::Queue& m_TransferQueue;
};
