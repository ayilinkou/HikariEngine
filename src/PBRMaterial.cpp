#include "PBRMaterial.h"

#include "assimp/material.h"

#include "ResourceManager.h"
#include "Utility.h"

PBRMaterial::PBRMaterial(vk::raii::Device& device,
                         vk::raii::PhysicalDevice& physicalDevice,
                         vk::raii::DescriptorPool& descriptorPool,
                         vk::raii::DescriptorSetLayout& setLayout,
                         vk::raii::Sampler& sampler, aiMaterial* mat,
                         const std::string& texturesParentFolder)
    : Material(mat->GetName().C_Str())
{
    LoadTextures(mat, texturesParentFolder);
    CreateUniformBuffer(device, physicalDevice);
    CreateDescriptorSet(device, descriptorPool, setLayout, sampler);
}

PBRMaterial::~PBRMaterial() { Shutdown(); }

void PBRMaterial::Shutdown()
{
    if (m_Albedo)
        ResourceManager::Get()->UnloadTexture(m_Albedo->GetPath());
    if (m_Normal)
        ResourceManager::Get()->UnloadTexture(m_Normal->GetPath());
    if (m_MetallicRoughness)
        ResourceManager::Get()->UnloadTexture(m_MetallicRoughness->GetPath());
}

void PBRMaterial::LoadTextures(aiMaterial* mat,
                               const std::string& texturesParentFolder)
{
    aiString texturePath;

    // TODO: read material values and assign in m_MaterialData if texture is not
    // found
    // TODO: set bTwoSided and AO

    // use BASE_COLOR if available, DIFFUSE as fallback
    if (mat->GetTexture(aiTextureType::aiTextureType_BASE_COLOR, 0,
                        &texturePath) == AI_SUCCESS ||
        mat->GetTexture(aiTextureType::aiTextureType_DIFFUSE, 0,
                        &texturePath) == AI_SUCCESS)
    {
        std::string path = texturesParentFolder + texturePath.C_Str();
        m_Albedo = ResourceManager::Get()->LoadTexture(
            path, vk::Format::eR8G8B8A8Srgb);
        m_MaterialData.bHasAlbedoTex = true;
    }

    if (mat->GetTexture(aiTextureType::aiTextureType_NORMALS, 0,
                        &texturePath) == AI_SUCCESS)
    {
        std::string path = texturesParentFolder + texturePath.C_Str();
        m_Normal = ResourceManager::Get()->LoadTexture(
            path, vk::Format::eR8G8B8A8Unorm);
        m_MaterialData.bHasNormalTex = true;
    }

    if (mat->GetTexture(aiTextureType::aiTextureType_GLTF_METALLIC_ROUGHNESS, 0,
                        &texturePath) == AI_SUCCESS)
    {
        std::string path = texturesParentFolder + texturePath.C_Str();
        m_MetallicRoughness = ResourceManager::Get()->LoadTexture(
            path, vk::Format::eR8G8B8A8Unorm);
        m_MaterialData.bHasMetallicRoughnessTex = true;
    }
}

void PBRMaterial::CreateUniformBuffer(vk::raii::Device& device,
                                      vk::raii::PhysicalDevice& physicalDevice)
{
    vk::DeviceSize size = sizeof(MaterialData);
    CreateBuffer(device, physicalDevice, size,
                 vk::BufferUsageFlagBits::eUniformBuffer,
                 vk::MemoryPropertyFlagBits::eHostVisible |
                     vk::MemoryPropertyFlagBits::eHostCoherent,
                 m_Buffer, m_BufferMemory);
    SetVkDebugName(device, *m_Buffer, vk::ObjectType::eBuffer,
                   (m_Name + " material buffer").c_str());
    SetVkDebugName(device, *m_BufferMemory, vk::ObjectType::eDeviceMemory,
                   (m_Name + " material buffer memory").c_str());

    void* data = m_BufferMemory.mapMemory(0u, size);
    memcpy(data, &m_MaterialData, sizeof(MaterialData));
    m_BufferMemory.unmapMemory();
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
        .imageView = m_Albedo ? *m_Albedo->GetImageView() : VK_NULL_HANDLE,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
    vk::DescriptorImageInfo normalInfo{
        .sampler = sampler,
        .imageView = m_Normal ? *m_Normal->GetImageView() : VK_NULL_HANDLE,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
    vk::DescriptorImageInfo metallicRoughnessInfo{
        .sampler = sampler,
        .imageView = m_MetallicRoughness ? *m_MetallicRoughness->GetImageView()
                                         : VK_NULL_HANDLE,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};

    std::vector<vk::WriteDescriptorSet> writeDescriptors;
    if (m_Albedo)
    {
        vk::WriteDescriptorSet albedoWriteSet{
            .dstSet = m_DescriptorSet,
            .dstBinding = TextureBinding::Albedo,
            .dstArrayElement = 0u,
            .descriptorCount = 1u,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .pImageInfo = &albedoInfo};
        writeDescriptors.push_back(albedoWriteSet);
    }

    if (m_Normal)
    {
        vk::WriteDescriptorSet normalWriteSet{
            .dstSet = m_DescriptorSet,
            .dstBinding = TextureBinding::Normal,
            .dstArrayElement = 0u,
            .descriptorCount = 1u,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .pImageInfo = &normalInfo};
        writeDescriptors.push_back(normalWriteSet);
    }

    if (m_MetallicRoughness)
    {
        vk::WriteDescriptorSet metallicRoughnessWriteSet{
            .dstSet = m_DescriptorSet,
            .dstBinding = TextureBinding::MetallicRoughness,
            .dstArrayElement = 0u,
            .descriptorCount = 1u,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .pImageInfo = &metallicRoughnessInfo};
        writeDescriptors.push_back(metallicRoughnessWriteSet);
    };

    vk::DescriptorBufferInfo bufferInfo{
        .buffer = *m_Buffer, .offset = 0u, .range = sizeof(MaterialData)};
    vk::WriteDescriptorSet matDataWriteSet{
        .dstSet = m_DescriptorSet,
        .dstBinding = TextureBinding::COUNT,
        .dstArrayElement = 0u,
        .descriptorCount = 1u,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .pBufferInfo = &bufferInfo};
    writeDescriptors.push_back(matDataWriteSet);

    device.updateDescriptorSets(writeDescriptors, {});
}
