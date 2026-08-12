#include "ResourceManager.h"

#include "Cubemap.h"
#include "CubemapLoader.h"
#include "Log.h"
#include "ModelData.h"
#include "ModelLoader.h"
#include "ModelManager.h"
#include "MyMacros.h"
#include "Texture.h"
#include "TextureLoader.h"

inline constexpr LogCategory LogResourceManager{"Resource Manager"};
constexpr std::string_view fallbackTexturePrefix = "FallbackTexture";

void ResourceManager::Init(vk::raii::Device& device,
                           vk::raii::PhysicalDevice& physicalDevice,
                           vk::raii::CommandPool& commandPool,
                           vk::raii::Queue& transferQueue,
                           VmaAllocator allocator)
{
    LogMsg(LogSeverity::Info, LogResourceManager, "Init()");

    if (s_Instance)
        throw std::runtime_error(
            "ResourceManager singleton has already been initialised!");

    s_Instance = new ResourceManager();
    TextureLoader::Init(device, physicalDevice, commandPool, transferQueue,
                        allocator);
    CubemapLoader::Init(device, physicalDevice, commandPool, transferQueue,
                        allocator);
    ModelLoader::Init(device, physicalDevice, commandPool, transferQueue,
                      allocator);
    ModelManager::Init();
}

void ResourceManager::Shutdown()
{
    LogMsg(LogSeverity::Info, LogResourceManager, "Shutdown()");

    if (!s_Instance)
        throw std::runtime_error(
            "Attempting to shutdown ResourceManager when it is already null!");

    ModelManager::Shutdown();
    ModelLoader::Shutdown();
    CubemapLoader::Shutdown();
    TextureLoader::Shutdown();

    assert(s_Instance->m_TextureCache.LiveCount() == 0);
    assert(s_Instance->m_CubemapCache.LiveCount() == 0);
    assert(s_Instance->m_ModelCache.LiveCount() == 0);

    delete s_Instance;
    s_Instance = nullptr;
}

void ResourceManager::PurgeCaches()
{
    if (!s_Instance)
        return;

    s_Instance->m_TextureCache.Purge();
    s_Instance->m_CubemapCache.Purge();
    s_Instance->m_ModelCache.Purge();
}

std::shared_ptr<Texture>
ResourceManager::LoadTexture(const std::string& filepath,
                             const vk::Format format)
{
    const std::string key = filepath + std::to_string(static_cast<uint32_t>(format));
    auto tex = m_TextureCache.Get(
        key,
        [&] { return TextureLoader::Get()->Load(filepath, format); });
    if (!tex)
    {
        tex.reset();
        const std::string fallbackTextureKey = std::string(fallbackTexturePrefix) + std::to_string(static_cast<uint32_t>(format));
        return m_TextureCache.Get(fallbackTextureKey, [&] { return TextureLoader::Get()->LoadFallbackTexture(format); });
    }
    return tex;
}

std::shared_ptr<Cubemap>
ResourceManager::LoadCubemap(const CubemapCreateInfo& createInfo)
{
    return m_CubemapCache.Get(
        createInfo.Key(),
        [&] { return CubemapLoader::Get()->Load(createInfo); });
}

std::shared_ptr<ModelData>
ResourceManager::LoadModel(const std::string& modelPath)
{
    return m_ModelCache.Get(modelPath, [&]
                            { return ModelLoader::Get()->Load(modelPath); });
}
