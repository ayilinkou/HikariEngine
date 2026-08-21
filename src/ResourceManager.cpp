#include "ResourceManager.h"

#include "CubemapLoader.h"
#include "ModelData.h"
#include "ModelLoader.h"
#include "ModelManager.h"
#include "TextureLoader.h"
#include <core/Log.h>
#include <core/MyMacros.h>

#include "Cubemap.h"
#include "Texture.h"

inline constexpr LogCategory LogResourceManager{"Resource Manager"};
constexpr std::string_view fallbackTexturePrefix = "FallbackTexture";

void ResourceManager::Init(Rhi::IDevice& rhiDevice, vk::raii::CommandPool& commandPool,
                           vk::raii::Queue& transferQueue, const Paths& paths)
{
    LogMsg(LogSeverity::Info, LogResourceManager, "Init()");

    if (s_Instance)
        throw std::runtime_error("ResourceManager singleton has already been initialised!");

    s_Instance = new ResourceManager(paths);
    TextureLoader::Init(rhiDevice, commandPool, transferQueue);
    CubemapLoader::Init(rhiDevice, commandPool, transferQueue);
    ModelLoader::Init(rhiDevice, commandPool, transferQueue);
    ModelManager::Init();
}

void ResourceManager::Shutdown()
{
    LogMsg(LogSeverity::Info, LogResourceManager, "Shutdown()");

    if (!s_Instance)
        throw std::runtime_error("Attempting to shutdown ResourceManager when it is already null!");

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

    uint32_t count = 0u;
    count += s_Instance->m_TextureCache.Purge();
    count += s_Instance->m_CubemapCache.Purge();
    count += s_Instance->m_ModelCache.Purge();

    if (count > 0u)
        LogMsg(LogSeverity::Info, LogResourceManager, "Purged {} expired resource entries.", count);
}

std::shared_ptr<Texture> ResourceManager::LoadTexture(const std::string& filepath,
                                                      const Rhi::Format format)
{
    // Keyed on the resolved path so that the same file requested relatively
    // (from a scene) and absolutely (from a model's own texture references)
    // shares one cache entry.
    const std::string resolved = m_Paths.Content(filepath).string();
    const std::string key = resolved + std::to_string(static_cast<uint32_t>(format));
    auto tex =
        m_TextureCache.Get(key, [&] { return TextureLoader::Get()->Load(resolved, format); });
    if (!tex)
    {
        tex.reset();
        const std::string fallbackTextureKey =
            std::string(fallbackTexturePrefix) + std::to_string(static_cast<uint32_t>(format));
        return m_TextureCache.Get(fallbackTextureKey, [&]
                                  { return TextureLoader::Get()->LoadFallbackTexture(format); });
    }
    return tex;
}

std::shared_ptr<Cubemap> ResourceManager::LoadCubemap(const CubemapCreateInfo& createInfo)
{
    return m_CubemapCache.Get(createInfo.Key(),
                              [&] { return CubemapLoader::Get()->Load(createInfo); });
}

std::shared_ptr<ModelData> ResourceManager::LoadModel(const std::string& modelPath)
{
    // ModelLoader derives its texture directory from the path it is given, so
    // handing it the resolved path is also what makes the model's own texture
    // references resolve.
    const std::string resolved = m_Paths.Content(modelPath).string();
    return m_ModelCache.Get(resolved, [&] { return ModelLoader::Get()->Load(resolved); });
}
