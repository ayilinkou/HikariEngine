#pragma once

#include <string>
#include <memory>

#include "vulkan/vulkan_raii.hpp"

#include "PBRMaterial.h"

struct aiScene;

class ModelData;

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

    [[nodiscard]] ModelData* Load(const std::string& path);

    static std::vector<std::unique_ptr<Material>>
    LoadMaterials(const aiScene* pScene, const std::string& modelRoot);

private:
    inline static ModelLoader* s_Instance = nullptr;

    vk::raii::Device& m_Device;
    vk::raii::PhysicalDevice& m_PhysicalDevice;
    vk::raii::CommandPool& m_CommandPool;
    vk::raii::Queue& m_TransferQueue;
};
