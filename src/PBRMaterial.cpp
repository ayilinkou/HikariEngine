#include "PBRMaterial.h"

#include <string>

#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_raii.hpp"

#include "assimp/material.h"

PBRMaterial::PBRMaterial(vk::raii::Device& device,
                         vk::raii::PhysicalDevice& physicalDevice,
                         vk::raii::CommandPool& commandPool,
                         vk::raii::Queue& transferQueue,
                         vk::raii::DescriptorPool& descriptorPool,
                         vk::raii::DescriptorSetLayout& setLayout,
                         vk::raii::Sampler& sampler, aiMaterial* mat,
                         const std::string& texturesParentFolder)
{
    LoadTextures(device, physicalDevice, commandPool, transferQueue, mat,
                 texturesParentFolder);
    CreateDescriptorSet(device, descriptorPool, setLayout, sampler);
}

void PBRMaterial::LoadTextures(vk::raii::Device& device,
                               vk::raii::PhysicalDevice& physicalDevice,
                               vk::raii::CommandPool& commandPool,
                               vk::raii::Queue& transferQueue, aiMaterial* mat,
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
        m_Albedo = std::make_unique<Texture>();
        m_Albedo->LoadTexture(device, physicalDevice, commandPool,
                              transferQueue, path, vk::Format::eR8G8B8A8Srgb);
    }

    if (mat->GetTexture(aiTextureType::aiTextureType_NORMALS, 0,
                        &texturePath) == AI_SUCCESS)
    {
        std::string path = texturesParentFolder + texturePath.C_Str();
        m_Normal = std::make_unique<Texture>();
        m_Normal->LoadTexture(device, physicalDevice, commandPool,
                              transferQueue, path, vk::Format::eR8G8B8A8Unorm);
    }

    if (mat->GetTexture(aiTextureType::aiTextureType_GLTF_METALLIC_ROUGHNESS, 0,
                        &texturePath) == AI_SUCCESS)
    {
        std::string path = texturesParentFolder + texturePath.C_Str();
        m_MetallicRoughness = std::make_unique<Texture>();
        m_MetallicRoughness->LoadTexture(device, physicalDevice, commandPool,
                                         transferQueue, path,
                                         vk::Format::eR8G8B8A8Unorm);
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
