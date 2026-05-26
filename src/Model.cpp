#include "Model.h"
#include "Utility.h"

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "vulkan/vulkan.hpp"
#include <vulkan/vulkan_raii.hpp>

void Model::LoadModel(vk::raii::Device& device,
                      vk::raii::PhysicalDevice& physicalDevice,
                      vk::raii::CommandPool& commandPool,
                      vk::raii::Queue& transferQueue,
					  vk::raii::DescriptorPool& descriptorPool,
					  vk::raii::DescriptorSetLayout& materialSetLayout,
                      const vk::raii::Sampler& sampler, const std::string& path)
{
    m_Name = path;

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

    CreateVertexBuffer(device, physicalDevice, commandPool, transferQueue);
    CreateIndexBuffer(device, physicalDevice, commandPool, transferQueue);
    LoadTextures(device, physicalDevice, commandPool, transferQueue);
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
                         vk::raii::Queue& transferQueue)
{
    const std::string ALBEDO_PATH = "models/pbr-case/textures/albedo.png";
    const std::string NORMAL_PATH = "models/pbr-case/textures/normal.png";
    const std::string ROUGHNESS_PATH = "models/pbr-case/textures/roughness.png";
    const std::string METALLIC_PATH = "models/pbr-case/textures/metallic.png";
    const std::string AO_PATH = "models/pbr-case/textures/ao.png";

    m_Albedo = std::make_unique<Texture>();
    m_Albedo->LoadTexture(device, physicalDevice, commandPool, transferQueue,
                          ALBEDO_PATH, vk::Format::eR8G8B8A8Srgb);
    m_Normal = std::make_unique<Texture>();
    m_Normal->LoadTexture(device, physicalDevice, commandPool, transferQueue,
                          NORMAL_PATH, vk::Format::eR8G8B8A8Unorm);
    m_Roughness = std::make_unique<Texture>();
    m_Roughness->LoadTexture(device, physicalDevice, commandPool, transferQueue,
                             ROUGHNESS_PATH, vk::Format::eR8G8B8A8Unorm);
    m_Metallic = std::make_unique<Texture>();
    m_Metallic->LoadTexture(device, physicalDevice, commandPool, transferQueue,
                            METALLIC_PATH, vk::Format::eR8G8B8A8Unorm);
    m_AO = std::make_unique<Texture>();
    m_AO->LoadTexture(device, physicalDevice, commandPool, transferQueue,
                      AO_PATH, vk::Format::eR8G8B8A8Unorm);
}

void Model::CreateDescriptorSet(vk::raii::Device& device,
                                vk::raii::DescriptorPool& descriptorPool,
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
    vk::DescriptorImageInfo roughnessInfo{
        .sampler = sampler,
        .imageView = m_Roughness->GetImageView(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
    vk::DescriptorImageInfo metallicInfo{
        .sampler = sampler,
        .imageView = m_Metallic->GetImageView(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
    vk::DescriptorImageInfo AOinfo{.sampler = sampler,
                                   .imageView = m_AO->GetImageView(),
                                   .imageLayout =
                                       vk::ImageLayout::eShaderReadOnlyOptimal};

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
                               .dstBinding = TextureBinding::Roughness,
                               .dstArrayElement = 0,
                               .descriptorCount = 1,
                               .descriptorType =
                                   vk::DescriptorType::eCombinedImageSampler,
                               .pImageInfo = &roughnessInfo},
        vk::WriteDescriptorSet{.dstSet = m_DescriptorSet,
                               .dstBinding = TextureBinding::Metallic,
                               .dstArrayElement = 0,
                               .descriptorCount = 1,
                               .descriptorType =
                                   vk::DescriptorType::eCombinedImageSampler,
                               .pImageInfo = &metallicInfo},
        vk::WriteDescriptorSet{.dstSet = m_DescriptorSet,
                               .dstBinding = TextureBinding::AO,
                               .dstArrayElement = 0,
                               .descriptorCount = 1,
                               .descriptorType =
                                   vk::DescriptorType::eCombinedImageSampler,
                               .pImageInfo = &AOinfo}};

    device.updateDescriptorSets(writeDescriptors, {});
}
