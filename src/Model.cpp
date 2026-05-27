#include "Model.h"

#include <filesystem>
#include <iostream>

#include "assimp/Importer.hpp"
#include "assimp/material.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include "vulkan/vulkan.hpp"
#include <assimp/types.h>
#include <vulkan/vulkan_raii.hpp>

#include "Utility.h"

void Model::LoadModel(vk::raii::Device& device,
                      vk::raii::PhysicalDevice& physicalDevice,
                      vk::raii::CommandPool& commandPool,
                      vk::raii::Queue& transferQueue,
                      vk::raii::DescriptorPool& descriptorPool,
                      vk::raii::DescriptorSetLayout& materialSetLayout,
                      const vk::raii::Sampler& sampler, const std::string& path)
{
    m_Name = path;
    m_Path = path;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path.data(), aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
                         aiProcess_CalcTangentSpace);

    if (!scene)
        throw std::runtime_error(std::format("Failed to load model: {}", path));

    aiMesh* mesh = scene->mMeshes[0];
    for (size_t i = 0; i < mesh->mNumVertices; i++)
    {
        Vertex v;
        v.Pos = {mesh->mVertices[i].x, mesh->mVertices[i].y,
                 mesh->mVertices[i].z};
        if (mesh->mTextureCoords[0])
        {
            v.TexCoord = {mesh->mTextureCoords[0][i].x,
                          1.f - mesh->mTextureCoords[0][i].y};
        }
        v.Color = {1.f, 0.f, 0.f};
        v.Normal = {mesh->mNormals[i].x, mesh->mNormals[i].y,
                    mesh->mNormals[i].z};

        assert(mesh->HasTangentsAndBitangents() &&
               "Mesh does not have tangents and bitangents!");
        v.Tangent = {mesh->mTangents[i].x, mesh->mTangents[i].y,
                     mesh->mTangents[i].z};

        m_Vertices.push_back(v);
    }

    for (size_t i = 0; i < mesh->mNumFaces; i++)
    {
        const aiFace& face = mesh->mFaces[i];
        for (size_t j = 0; j < face.mNumIndices; j++)
        {
            m_Indices.push_back(static_cast<uint32_t>(face.mIndices[j]));
        }
    }

    aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];

    CreateVertexBuffer(device, physicalDevice, commandPool, transferQueue);
    CreateIndexBuffer(device, physicalDevice, commandPool, transferQueue);
    LoadTextures(device, physicalDevice, commandPool, transferQueue, mat);
    CreateDescriptorSet(device, descriptorPool, materialSetLayout, sampler);
}

void Model::CreateVertexBuffer(vk::raii::Device& device,
                               vk::raii::PhysicalDevice& physicalDevice,
                               vk::raii::CommandPool& commandPool,
                               vk::raii::Queue& transferQueue)
{
    vk::DeviceSize bufferSize = sizeof(m_Vertices[0]) * m_Vertices.size();

    vk::raii::Buffer stagingBuffer({});
    vk::raii::DeviceMemory stagingBufferMemory({});
    CreateBuffer(device, physicalDevice, bufferSize,
                 vk::BufferUsageFlagBits::eTransferSrc,
                 vk::MemoryPropertyFlagBits::eHostCoherent |
                     vk::MemoryPropertyFlagBits::eHostCoherent,
                 stagingBuffer, stagingBufferMemory);

    void* dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(dataStaging, m_Vertices.data(), static_cast<size_t>(bufferSize));
    stagingBufferMemory.unmapMemory();

    CreateBuffer(device, physicalDevice, bufferSize,
                 vk::BufferUsageFlagBits::eVertexBuffer |
                     vk::BufferUsageFlagBits::eTransferDst,
                 vk::MemoryPropertyFlagBits::eDeviceLocal, m_VertexBuffer,
                 m_VertexMemory);

    CopyBuffer(device, commandPool, transferQueue, stagingBuffer,
               m_VertexBuffer, bufferSize);
}

void Model::CreateIndexBuffer(vk::raii::Device& device,
                              vk::raii::PhysicalDevice& physicalDevice,
                              vk::raii::CommandPool& commandPool,
                              vk::raii::Queue& transferQueue)
{
    vk::DeviceSize bufferSize = sizeof(m_Indices[0]) * m_Indices.size();

    vk::raii::Buffer stagingBuffer({});
    vk::raii::DeviceMemory stagingBufferMemory({});
    CreateBuffer(device, physicalDevice, bufferSize,
                 vk::BufferUsageFlagBits::eTransferSrc,
                 vk::MemoryPropertyFlagBits::eHostCoherent |
                     vk::MemoryPropertyFlagBits::eHostCoherent,
                 stagingBuffer, stagingBufferMemory);

    void* dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(dataStaging, m_Indices.data(), static_cast<size_t>(bufferSize));
    stagingBufferMemory.unmapMemory();

    CreateBuffer(device, physicalDevice, bufferSize,
                 vk::BufferUsageFlagBits::eIndexBuffer |
                     vk::BufferUsageFlagBits::eTransferDst,
                 vk::MemoryPropertyFlagBits::eDeviceLocal, m_IndexBuffer,
                 m_IndexMemory);

    CopyBuffer(device, commandPool, transferQueue, stagingBuffer, m_IndexBuffer,
               bufferSize);
}

void Model::LoadTextures(vk::raii::Device& device,
                         vk::raii::PhysicalDevice& physicalDevice,
                         vk::raii::CommandPool& commandPool,
                         vk::raii::Queue& transferQueue, aiMaterial* mat)
{
    std::filesystem::path modelPath = m_Path;
    std::string modelRoot = modelPath.parent_path().string() + "/";
    aiString texturePath;

    // use BASE_COLOR if available, DIFFUSE as fallback
    if (mat->GetTexture(aiTextureType::aiTextureType_BASE_COLOR, 0,
                        &texturePath) == AI_SUCCESS ||
        mat->GetTexture(aiTextureType::aiTextureType_DIFFUSE, 0,
                        &texturePath) == AI_SUCCESS)
    {
        std::string path = modelRoot + texturePath.C_Str();
        m_Albedo = std::make_unique<Texture>();
        m_Albedo->LoadTexture(device, physicalDevice, commandPool,
                              transferQueue, path, vk::Format::eR8G8B8A8Srgb);
    }

    if (mat->GetTexture(aiTextureType::aiTextureType_NORMALS, 0,
                        &texturePath) == AI_SUCCESS)
    {
        std::string path = modelRoot + texturePath.C_Str();
        m_Normal = std::make_unique<Texture>();
        m_Normal->LoadTexture(device, physicalDevice, commandPool,
                              transferQueue, path, vk::Format::eR8G8B8A8Unorm);
    }

    if (mat->GetTexture(aiTextureType::aiTextureType_GLTF_METALLIC_ROUGHNESS, 0,
                        &texturePath) == AI_SUCCESS)
    {
        std::string path = modelRoot + texturePath.C_Str();
        m_MetallicRoughness = std::make_unique<Texture>();
        m_MetallicRoughness->LoadTexture(device, physicalDevice, commandPool,
                                         transferQueue, path,
                                         vk::Format::eR8G8B8A8Unorm);
    }
}

void Model::CreateDescriptorSet(
    vk::raii::Device& device, vk::raii::DescriptorPool& descriptorPool,
    vk::raii::DescriptorSetLayout& materialSetLayout,
    const vk::raii::Sampler& sampler)
{
    std::vector<vk::DescriptorSetLayout> layouts(1, *materialSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo{
        .descriptorPool = *descriptorPool,
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
