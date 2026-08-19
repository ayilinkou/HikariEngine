#pragma once

#include <memory>
#include <string>
#include <vector>

#include "vulkan/vulkan_raii.hpp"

#include <rhi/IDevice.h>

#include "Material.h"

struct aiScene;

class ModelData;

class ModelLoader
{
private:
    friend class ResourceManager;

    ModelLoader(Rhi::IDevice& rhiDevice, vk::raii::Device& device,
                vk::raii::PhysicalDevice& physicalDevice, vk::raii::CommandPool& commandPool,
                vk::raii::Queue& transferQueue);

    static void Init(Rhi::IDevice& rhiDevice, vk::raii::Device& device,
                     vk::raii::PhysicalDevice& physicalDevice, vk::raii::CommandPool& commandPool,
                     vk::raii::Queue& transferQueue);
    static void Shutdown();

    static ModelLoader* Get() { return s_Instance; }

    [[nodiscard]] std::shared_ptr<ModelData> Load(const std::string& path);

    static std::vector<std::unique_ptr<Material>> LoadMaterials(const aiScene* pScene,
                                                                const std::string& modelRoot);

private:
    inline static ModelLoader* s_Instance = nullptr;

    Rhi::IDevice& m_RhiDevice;
    vk::raii::Device& m_Device;
    vk::raii::PhysicalDevice& m_PhysicalDevice;
    vk::raii::CommandPool& m_CommandPool;
    vk::raii::Queue& m_TransferQueue;
};
