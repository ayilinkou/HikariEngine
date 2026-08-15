#pragma once

#include "vk_mem_alloc.h"
#include "vulkan/vulkan_raii.hpp"

#include "ResourceCache.h"

struct CubemapCreateInfo;

class Texture;
class Cubemap;
class ModelData;

class ResourceManager
{
private:
    ResourceManager() {}

public:
    static void Init(vk::raii::Device& device, vk::raii::PhysicalDevice& physicalDevice,
                     vk::raii::CommandPool& commandPool, vk::raii::Queue& transferQueue,
                     VmaAllocator allocator);
    static ResourceManager* Get() { return s_Instance; }

private:
    inline static ResourceManager* s_Instance = nullptr;

public:
    static void PurgeCaches();
    static void Shutdown();

    std::shared_ptr<Texture> LoadTexture(const std::string& filepath, const vk::Format format);
    std::shared_ptr<Cubemap> LoadCubemap(const CubemapCreateInfo& createInfo);
    std::shared_ptr<ModelData> LoadModel(const std::string& modelPath);

private:
    ResourceCache<Texture> m_TextureCache;
    ResourceCache<Cubemap> m_CubemapCache;
    ResourceCache<ModelData> m_ModelCache;
};
