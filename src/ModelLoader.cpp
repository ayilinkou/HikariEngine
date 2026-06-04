#include "ModelLoader.h"

#include <filesystem>
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include "MaterialFactory.h"
#include "ModelData.h"
#include "Utility.h"
#include "Vertex.h"

ModelLoader::ModelLoader(vk::raii::Device& device,
                         vk::raii::PhysicalDevice& physicalDevice,
                         vk::raii::CommandPool& commandPool,
                         vk::raii::Queue& transferQueue)
    : m_Device(device), m_PhysicalDevice(physicalDevice),
      m_CommandPool(commandPool), m_TransferQueue(transferQueue)
{
}

void ModelLoader::Init(vk::raii::Device& device,
                       vk::raii::PhysicalDevice& physicalDevice,
                       vk::raii::CommandPool& commandPool,
                       vk::raii::Queue& transferQueue)
{
    if (s_Instance)
        throw std::runtime_error(
            "ModelLoader singleton is already initialised!");

    s_Instance =
        new ModelLoader(device, physicalDevice, commandPool, transferQueue);
}

void ModelLoader::Shutdown()
{
    if (!s_Instance)
        throw std::runtime_error(
            "Attempting to shutdown ModelLoader when instance is null!");

    delete s_Instance;
    s_Instance = nullptr;
}

ModelData* ModelLoader::Load(const std::string& path)
{
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path.data(), aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
                         aiProcess_CalcTangentSpace);

    if (!scene)
        throw std::runtime_error(std::format("Failed to load model: {}", path));

    std::vector<Vertex> vertices;
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
        v.Normal = {mesh->mNormals[i].x, mesh->mNormals[i].y,
                    mesh->mNormals[i].z};

        assert(mesh->HasTangentsAndBitangents() &&
               "Mesh does not have tangents and bitangents!");
        glm::vec3 tangent = {mesh->mTangents[i].x, mesh->mTangents[i].y,
                             mesh->mTangents[i].z};
        glm::vec3 bitangent = {mesh->mBitangents[i].x, mesh->mBitangents[i].y,
                               mesh->mBitangents[i].z};
        float handedness =
            (glm::dot(glm::cross(v.Normal, tangent), bitangent) > 0.f) ? -1.f
                                                                       : 1.f;
        v.Tangent = glm::vec4(tangent, handedness);

        vertices.push_back(v);
    }

    std::vector<uint32_t> indices;
    for (size_t i = 0; i < mesh->mNumFaces; i++)
    {
        const aiFace& face = mesh->mFaces[i];
        for (size_t j = 0; j < face.mNumIndices; j++)
        {
            indices.push_back(static_cast<uint32_t>(face.mIndices[j]));
        }
    }

    aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];

    vk::raii::Buffer vertexBuffer({});
    vk::raii::DeviceMemory vertexMemory({});
    CreateVertexBuffer(m_Device, m_PhysicalDevice, m_CommandPool,
                       m_TransferQueue, sizeof(vertices[0]), vertices.size(),
                       vertices.data(), vertexBuffer, vertexMemory);
    SetVkDebugName(m_Device, *vertexBuffer, vk::ObjectType::eBuffer,
                   std::format("{} Vertex Buffer", path).c_str());
    SetVkDebugName(m_Device, *vertexMemory, vk::ObjectType::eDeviceMemory,
                   std::format("{} Vertex Buffer Memory", path).c_str());

    vk::raii::Buffer indexBuffer({});
    vk::raii::DeviceMemory indexMemory({});
    CreateIndexBuffer(m_Device, m_PhysicalDevice, m_CommandPool,
                      m_TransferQueue, sizeof(indices[0]), indices.size(),
                      indices.data(), indexBuffer, indexMemory);
    SetVkDebugName(m_Device, *indexBuffer, vk::ObjectType::eBuffer,
                   std::format("{} Index Buffer", path).c_str());
    SetVkDebugName(m_Device, *indexMemory, vk::ObjectType::eDeviceMemory,
                   std::format("{} Index Buffer Memory", path).c_str());

    std::filesystem::path modelPath = path;
    std::string modelRoot = modelPath.parent_path().string() + "/";
    Material* material =
        MaterialFactory::Get()->CreatePBRMaterial(mat, modelRoot);

    return new ModelData(std::move(vertexBuffer), std::move(vertexMemory),
                         std::move(indexBuffer), std::move(indexMemory),
                         material, indices.size(), path);
}
