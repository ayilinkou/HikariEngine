#pragma once

#include <memory>

#include "vulkan/vulkan.hpp"

#include "Material.h"
#include "PBRMaterial.h"

struct aiMaterial;

class MaterialFactory
{
public:
    static void Init(vk::raii::Device& device, vk::raii::Sampler& sampler);
    static void Shutdown();

    static MaterialFactory* Get() { return s_Instance; }

    [[nodiscard]] PBRMaterial*
    CreatePBRMaterial(aiMaterial* mat, const std::string& texturesParentFolder);

    vk::DescriptorSetLayout GetDescriptorSetLayout() const
    {
        return *m_SetLayout;
    }

private:
    MaterialFactory(vk::raii::Device& device, vk::raii::Sampler& sampler);

    void CreateDescriptorPool();
    void CreateDescriptorSetLayout();

private:
    static MaterialFactory* s_Instance;

    vk::raii::DescriptorSetLayout m_SetLayout = nullptr;
    vk::raii::DescriptorPool m_DescriptorPool = nullptr;

    vk::raii::Device& m_Device;
    vk::raii::Sampler& m_Sampler;

    static const uint8_t s_MAX_TEXTURE_COUNT_PER_MAT;
    static const uint16_t s_MAX_MATERIAL_SET_COUNT;
};
