#pragma once

#include "vulkan/vulkan_raii.hpp"

#include "Material.h"
#include "Texture.h"

struct aiMaterial;

class PBRMaterial : public Material
{
public:
    PBRMaterial(vk::raii::Device& device,
                vk::raii::PhysicalDevice& physicalDevice,
                vk::raii::DescriptorPool& descriptorPool,
                vk::raii::DescriptorSetLayout& setLayout,
                vk::raii::Sampler& sampler, aiMaterial* mat,
                const std::string& texturesParentFolder);
    virtual ~PBRMaterial() override;

private:
    void LoadTextures(aiMaterial* mat, const std::string& texturesParentFolder);
    void CreateUniformBuffer(vk::raii::Device& device,
                      vk::raii::PhysicalDevice& physicalDevice);
    void CreateDescriptorSet(vk::raii::Device& device,
                             vk::raii::DescriptorPool& descriptorPool,
                             vk::raii::DescriptorSetLayout& setLayout,
                             vk::raii::Sampler& sampler);
    void Shutdown();

private:
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
		glm::vec2 Padding{};
    };

private:
    Texture* m_Albedo = nullptr;
    Texture* m_Normal = nullptr;
    Texture* m_MetallicRoughness = nullptr;

    vk::raii::Buffer m_Buffer = nullptr;
    vk::raii::DeviceMemory m_BufferMemory = nullptr;
    MaterialData m_MaterialData{};
};
