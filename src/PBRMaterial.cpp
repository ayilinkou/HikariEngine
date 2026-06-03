#include "PBRMaterial.h"

#include <string>

#include "Utility.h"
#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_raii.hpp"

#include "assimp/material.h"

#include "ResourceManager.h"

PBRMaterial::PBRMaterial(vk::raii::Device& device,
                         vk::raii::DescriptorPool& descriptorPool,
                         vk::raii::DescriptorSetLayout& setLayout,
                         vk::raii::Sampler& sampler, aiMaterial* mat,
                         const std::string& texturesParentFolder)
    : Material(mat->GetName().C_Str())
{
    LoadTextures(mat, texturesParentFolder);
    CreateDescriptorSet(device, descriptorPool, setLayout, sampler);
}

PBRMaterial::~PBRMaterial() { Shutdown(); }

void PBRMaterial::Shutdown()
{
    ResourceManager::Get()->UnloadTexture(m_Albedo->GetPath());
    ResourceManager::Get()->UnloadTexture(m_Normal->GetPath());
    ResourceManager::Get()->UnloadTexture(m_MetallicRoughness->GetPath());
}

void PBRMaterial::LoadTextures(aiMaterial* mat,
                               const std::string& texturesParentFolder)
{
    aiString texturePath;

    // use BASE_COLOR if available, DIFFUSE as fallback
    if (mat->GetTexture(aiTextureType::aiTextureType_BASE_COLOR, 0,
                        &texturePath) == AI_SUCCESS ||
        mat->GetTexture(aiTextureType::aiTextureType_DIFFUSE, 0,
                        &texturePath) == AI_SUCCESS)
    {
        std::string path = texturesParentFolder + texturePath.C_Str();
        m_Albedo = ResourceManager::Get()->LoadTexture(
            path, vk::Format::eR8G8B8A8Srgb);
    }

    if (mat->GetTexture(aiTextureType::aiTextureType_NORMALS, 0,
                        &texturePath) == AI_SUCCESS)
    {
        std::string path = texturesParentFolder + texturePath.C_Str();
        m_Normal = ResourceManager::Get()->LoadTexture(
            path, vk::Format::eR8G8B8A8Unorm);
    }

    if (mat->GetTexture(aiTextureType::aiTextureType_GLTF_METALLIC_ROUGHNESS, 0,
                        &texturePath) == AI_SUCCESS)
    {
        std::string path = texturesParentFolder + texturePath.C_Str();
        m_MetallicRoughness = ResourceManager::Get()->LoadTexture(
            path, vk::Format::eR8G8B8A8Unorm);
    }
}

void PBRMaterial::CreateDescriptorSet(
    vk::raii::Device& device, vk::raii::DescriptorPool& descriptorPool,
    vk::raii::DescriptorSetLayout& materialSetLayout,
    vk::raii::Sampler& sampler)
{
    std::vector<vk::DescriptorSetLayout> layouts(1, materialSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo{
        .descriptorPool = descriptorPool,
        .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
        .pSetLayouts = layouts.data()};

    auto sets = device.allocateDescriptorSets(allocInfo);
    m_DescriptorSet = std::move(sets.front());
    SetVkDebugName(device, *m_DescriptorSet, vk::ObjectType::eDescriptorSet,
                   std::format("{} Material Descriptor Set", m_Name).c_str());

    vk::DescriptorImageInfo albedoInfo{
        .sampler = sampler,
        .imageView = m_Albedo->GetImageView(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
    vk::DescriptorImageInfo normalInfo{
        .sampler = sampler,
        .imageView = m_Normal->GetImageView(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
    vk::DescriptorImageInfo metallicRoughnessInfo{
        .sampler = sampler,
        .imageView = m_MetallicRoughness->GetImageView(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};

    std::array writeDescriptors{
        vk::WriteDescriptorSet{.dstSet = m_DescriptorSet,
                               .dstBinding = TextureBinding::Albedo,
                               .dstArrayElement = 0,
                               .descriptorCount = 1,
                               .descriptorType =
                                   vk::DescriptorType::eCombinedImageSampler,
                               .pImageInfo = &albedoInfo},
        vk::WriteDescriptorSet{.dstSet = m_DescriptorSet,
                               .dstBinding = TextureBinding::Normal,
                               .dstArrayElement = 0,
                               .descriptorCount = 1,
                               .descriptorType =
                                   vk::DescriptorType::eCombinedImageSampler,
                               .pImageInfo = &normalInfo},
        vk::WriteDescriptorSet{.dstSet = m_DescriptorSet,
                               .dstBinding = TextureBinding::MetallicRoughness,
                               .dstArrayElement = 0,
                               .descriptorCount = 1,
                               .descriptorType =
                                   vk::DescriptorType::eCombinedImageSampler,
                               .pImageInfo = &metallicRoughnessInfo}};

    device.updateDescriptorSets(writeDescriptors, {});
}
