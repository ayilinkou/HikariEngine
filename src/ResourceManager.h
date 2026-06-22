#pragma once

#include "Resource.h"

struct CubemapCreateInfo;

class Texture;
class Cubemap;
class ModelData;

class ResourceManager
{
private:
    ResourceManager() {}

public:
    static void Init(vk::raii::Device& device,
                     vk::raii::PhysicalDevice& physicalDevice,
                     vk::raii::CommandPool& commandPool,
                     vk::raii::Queue& transferQueue);
    static ResourceManager* Get() { return s_Instance; }

private:
    inline static ResourceManager* s_Instance = nullptr;

public:
    static void Shutdown();

    Texture* LoadTexture(const std::string& filepath, const vk::Format format);
    Cubemap* LoadCubemap(const CubemapCreateInfo& createInfo);
    ModelData* LoadModel(const std::string& modelPath);

    uint32_t UnloadTexture(const std::string& filepath);
    uint32_t UnloadCubemap(const CubemapCreateInfo& createInfo);
    uint32_t UnloadModel(const std::string& filepath);

private:
	// These are intentionally not references because the resources which are being
	// unloaded are providing the map key, and so need to make a copy to use after
	// they are deleted.
    void Internal_UnloadTexture(const std::string filepath);
    void Internal_UnloadCubemap(const CubemapCreateInfo createInfo);
    void Internal_UnloadModel(const std::string filepath);

private:
    std::unordered_map<std::string, std::unique_ptr<Resource>> m_TexturesMap;
    // Since cubemaps use 6 textures with 6 texture paths, storing in the map
    // will use the +X (right) path
    std::unordered_map<std::string, std::unique_ptr<Resource>> m_CubemapsMap;
    std::unordered_map<std::string, std::unique_ptr<Resource>> m_ModelsMap;
};
