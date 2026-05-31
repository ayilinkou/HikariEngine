#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "vulkan/vulkan_raii.hpp"

#include "Resource.h"

class Texture;
class Model;

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
    Model* LoadModel(const std::string& modelPath);

    uint32_t UnloadTexture(const std::string& filepath);
    uint32_t UnloadModel(const std::string& filepath);

    std::unordered_map<std::string, std::unique_ptr<Resource>>& GetTexturesMap()
    {
        return m_TexturesMap;
    }

    std::unordered_map<std::string, std::unique_ptr<Resource>>& GetModelsMap()
    {
        return m_ModelsMap;
    }

private:
    void Internal_UnloadTexture(const std::string filepath);
    void Internal_UnloadModel(const std::string filepath);

private:
    std::unordered_map<std::string, std::unique_ptr<Resource>> m_TexturesMap;
    std::unordered_map<std::string, std::unique_ptr<Resource>> m_ModelsMap;
};
