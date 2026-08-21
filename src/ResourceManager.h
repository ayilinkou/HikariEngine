#pragma once

#include <memory>
#include <string>

#include "vulkan/vulkan_raii.hpp"

#include <rhi/IDevice.h>
#include <rhi/RhiTypes.h>

#include <platform/Paths.h>

#include "ResourceCache.h"

struct CubemapCreateInfo;

class Texture;
class Cubemap;
class ModelData;

class ResourceManager
{
private:
    explicit ResourceManager(const Paths& paths) : m_Paths(paths) {}

public:
    static void Init(Rhi::IDevice& rhiDevice, vk::raii::CommandPool& commandPool,
                     vk::raii::Queue& transferQueue, const Paths& paths);
    static ResourceManager* Get() { return s_Instance; }

private:
    inline static ResourceManager* s_Instance = nullptr;

public:
    static void PurgeCaches();
    static void Shutdown();

    std::shared_ptr<Texture> LoadTexture(const std::string& filepath, const Rhi::Format format);
    std::shared_ptr<Cubemap> LoadCubemap(const CubemapCreateInfo& createInfo);
    std::shared_ptr<ModelData> LoadModel(const std::string& modelPath);

private:
    // Asset paths arrive here content-relative (a Model keeps the path it was
    // serialized with) and are resolved against the content root here, at the
    // point of loading.
    const Paths& m_Paths;

    ResourceCache<Texture> m_TextureCache;
    ResourceCache<Cubemap> m_CubemapCache;
    ResourceCache<ModelData> m_ModelCache;
};
