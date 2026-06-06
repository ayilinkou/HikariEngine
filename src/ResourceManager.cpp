#include "ResourceManager.h"

#include <stdexcept>

#include "ModelData.h"
#include "ModelLoader.h"
#include "ModelManager.h"
#include "MyMacros.h"
#include "Texture.h"
#include "TextureLoader.h"

void ResourceManager::Init(vk::raii::Device& device,
                           vk::raii::PhysicalDevice& physicalDevice,
                           vk::raii::CommandPool& commandPool,
                           vk::raii::Queue& transferQueue)
{
    if (s_Instance)
        throw std::runtime_error(
            "ResourceManager singleton has already been initialised!");

    s_Instance = new ResourceManager();
    TextureLoader::Init(device, physicalDevice, commandPool, transferQueue);
    ModelLoader::Init(device, physicalDevice, commandPool, transferQueue);
    ModelManager::Init();
}

void ResourceManager::Shutdown()
{
    if (!s_Instance)
        throw std::runtime_error(
            "Attempting to shutdown ResourceManager when it is already null!");

    ModelManager::Shutdown();
    ModelLoader::Shutdown();
    TextureLoader::Shutdown();

    if (!s_Instance->m_TexturesMap.empty() || !s_Instance->m_ModelsMap.empty())
    {
        DEBUG_BREAK(); // Attempting to shutdown when resources are still
                       // loaded!
    }

    s_Instance->m_ModelsMap.clear();
    s_Instance->m_TexturesMap.clear();
    delete s_Instance;
    s_Instance = nullptr;
}

Texture* ResourceManager::LoadTexture(const std::string& filepath,
                                      const vk::Format format)
{
    auto it = m_TexturesMap.find(filepath);
    if (it != m_TexturesMap.end() && it->second.get())
    {
        it->second->AddRef();
        return static_cast<Texture*>(it->second->m_pData);
    }

    Texture* pData = TextureLoader::Get()->Load(filepath, format);
    if (!pData)
        return nullptr;

    m_TexturesMap[filepath] = std::make_unique<Resource>(pData);
    return pData;
}

ModelData* ResourceManager::LoadModel(const std::string& modelPath)
{
    auto it = m_ModelsMap.find(modelPath);
    if (it != m_ModelsMap.end() && it->second.get())
    {
        it->second->AddRef();
        return static_cast<ModelData*>(it->second->m_pData);
    }

    ModelData* pData = ModelLoader::Get()->Load(modelPath);
    if (!pData)
        return nullptr;

    m_ModelsMap[modelPath] = std::make_unique<Resource>(pData);
    return pData;
}

uint32_t ResourceManager::UnloadTexture(const std::string& filepath)
{
    Resource* resourceToUnload = m_TexturesMap[filepath].get();
    if (!resourceToUnload)
    {
        m_TexturesMap.erase(filepath);
        return 0u;
    }

    resourceToUnload->RemoveRef();
    if (resourceToUnload->m_RefCount > 0u)
    {
        return resourceToUnload->m_RefCount;
    }

    Internal_UnloadTexture(filepath);
    return 0u;
}

uint32_t ResourceManager::UnloadModel(const std::string& filepath)
{
    Resource* ResourceToUnload = m_ModelsMap[filepath].get();
    if (!ResourceToUnload)
    {
        m_ModelsMap.erase(filepath);
        return 0u;
    }

    ResourceToUnload->RemoveRef();
    if (ResourceToUnload->m_RefCount > 0u)
    {
        return ResourceToUnload->m_RefCount;
    }

    Internal_UnloadModel(filepath);
    return 0u;
}

void ResourceManager::Internal_UnloadTexture(const std::string filepath)
{
    Texture* pTexture = static_cast<Texture*>(m_TexturesMap[filepath]->m_pData);
    delete pTexture;
    m_TexturesMap.erase(filepath);
}

void ResourceManager::Internal_UnloadModel(const std::string filepath)
{
    ModelData* pModelData =
        static_cast<ModelData*>(m_ModelsMap[filepath]->m_pData);
    delete pModelData;
    m_ModelsMap.erase(filepath);
}
