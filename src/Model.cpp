#include "Model.h"

#include <filesystem>

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include "vulkan/vulkan.hpp"
#include <vulkan/vulkan_raii.hpp>

#include "PBRMaterial.h"
#include "MaterialFactory.h"
#include "Utility.h"

void Model::LoadModel(vk::raii::Device& device,
                   vk::raii::PhysicalDevice& physicalDevice,
                   vk::raii::CommandPool& commandPool,
                   vk::raii::Queue& transferQueue,
                   const std::string& path)
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

    std::filesystem::path modelPath = m_Path;
    std::string modelRoot = modelPath.parent_path().string() + "/";
    m_Material.reset(MaterialFactory::Get()->CreatePBRMaterial(mat, modelRoot));
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
