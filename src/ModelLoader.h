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

    ModelLoader(Rhi::IDevice& rhiDevice, vk::raii::CommandPool& commandPool,
                vk::raii::Queue& transferQueue);

    static void Init(Rhi::IDevice& rhiDevice, vk::raii::CommandPool& commandPool,
                     vk::raii::Queue& transferQueue);
    static void Shutdown();

    static ModelLoader* Get() { return s_Instance; }

    [[nodiscard]] std::shared_ptr<ModelData> Load(const std::string& path);

    static std::vector<std::unique_ptr<Material>> LoadMaterials(const aiScene* pScene,
                                                                const std::string& modelRoot);

private:
    inline static ModelLoader* s_Instance = nullptr;

    Rhi::IDevice& m_RhiDevice;

    // Still Vulkan-shaped because each upload submits on its own and then waits
    // for the queue to drain. An upload context recording many copies behind a
    // single fence is what replaces both of these.
    vk::raii::CommandPool& m_CommandPool;
    vk::raii::Queue& m_TransferQueue;
};
