#pragma once

#include <string>

#include "vulkan/vulkan_raii.hpp"

class Model;

class ModelLoader
{
private:
    friend class ResourceManager;

    ModelLoader(vk::raii::Device& device,
                vk::raii::PhysicalDevice& physicalDevice,
                vk::raii::CommandPool& commandPool,
                vk::raii::Queue& transferQueue);

    static void Init(vk::raii::Device& device,
                     vk::raii::PhysicalDevice& physicalDevice,
                     vk::raii::CommandPool& commandPool,
                     vk::raii::Queue& transferQueue);
    static void Shutdown();

    static ModelLoader* Get() { return s_Instance; }

    [[nodiscard]] Model* Load(const std::string& path);

private:
    inline static ModelLoader* s_Instance = nullptr;

    vk::raii::Device& m_Device;
    vk::raii::PhysicalDevice& m_PhysicalDevice;
    vk::raii::CommandPool& m_CommandPool;
    vk::raii::Queue& m_TransferQueue;
};
