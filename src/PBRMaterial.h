#pragma once

#include "Material.h"
#include "Texture.h"
#include "vulkan/vulkan.hpp"

struct aiMaterial;

class PBRMaterial : public Material
{
public:
    PBRMaterial(vk::raii::Device& device,
                vk::raii::PhysicalDevice& physicalDevice,
                vk::raii::CommandPool& commandPool,
                vk::raii::Queue& transferQueue,
                vk::raii::DescriptorPool& descriptorPool,
                vk::raii::DescriptorSetLayout& setLayout,
                vk::raii::Sampler& sampler, aiMaterial* mat,
                const std::string& texturesParentFolder);
    virtual ~PBRMaterial() override = default;

private:
    void LoadTextures(vk::raii::Device& device,
                      vk::raii::PhysicalDevice& physicalDevice,
                      vk::raii::CommandPool& commandPool,
                      vk::raii::Queue& transferQueue, aiMaterial* mat,
                      const std::string& texturesParentFolder);
    void CreateDescriptorSet(vk::raii::Device& device,
                             vk::raii::DescriptorPool& descriptorPool,
                             vk::raii::DescriptorSetLayout& setLayout,
                             vk::raii::Sampler& sampler);

private:
    std::unique_ptr<Texture> m_Albedo = nullptr;
    std::unique_ptr<Texture> m_Normal = nullptr;
    std::unique_ptr<Texture> m_MetallicRoughness = nullptr;
};
