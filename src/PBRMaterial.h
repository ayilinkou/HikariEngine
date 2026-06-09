#pragma once

#include "vulkan/vulkan_raii.hpp"

#include "Material.h"
#include "Texture.h"

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

    virtual void* GetPushConstantData() override { return &m_MatData; }

private:
    void LoadTextures(aiMaterial* mat, const std::string& texturesParentFolder);
    void CreateDescriptorSet(vk::raii::Device& device,
                             vk::raii::DescriptorPool& descriptorPool,
                             vk::raii::DescriptorSetLayout& setLayout,
                             vk::raii::Sampler& sampler);
    void Shutdown();

public:
    struct MaterialData
    {
        glm::vec3 Albedo{1.f, 1.f, 1.f};
        float Metallic = 0.f;
        float Roughness = 1.f;
        float AO = 1.f;
        int bHasAlbedoTex = false;
        int bHasNormalTex = false;
        int bHasMetallicRoughnessTex = false;
        int bTwoSided = false;
    };

private:
    Texture* m_Albedo = nullptr;
    Texture* m_Normal = nullptr;
    Texture* m_MetallicRoughness = nullptr;

    MaterialData m_MatData{};
};
