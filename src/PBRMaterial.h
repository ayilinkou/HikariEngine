#pragma once

#include "Material.h"
#include "Texture.h"
#include "vulkan/vulkan.hpp"

struct aiMaterial;

class PBRMaterial : public Material
{
public:
    PBRMaterial(vk::raii::Device& device,
                vk::raii::DescriptorPool& descriptorPool,
                vk::raii::DescriptorSetLayout& setLayout,
                vk::raii::Sampler& sampler, aiMaterial* mat,
                const std::string& texturesParentFolder);
    virtual ~PBRMaterial() override;

private:
    void LoadTextures(aiMaterial* mat, const std::string& texturesParentFolder);
    void CreateDescriptorSet(vk::raii::Device& device,
                             vk::raii::DescriptorPool& descriptorPool,
                             vk::raii::DescriptorSetLayout& setLayout,
                             vk::raii::Sampler& sampler);
	void Shutdown();

private:
    Texture* m_Albedo = nullptr;
    Texture* m_Normal = nullptr;
    Texture* m_MetallicRoughness = nullptr;
};
