#include "ModelLoader.h"

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include "MaterialFactory.h"
#include "ModelData.h"
#include "Node.h"
#include "Utility.h"
#include "Vertex.h"

ModelLoader::ModelLoader(vk::raii::Device& device,
                         vk::raii::PhysicalDevice& physicalDevice,
                         vk::raii::CommandPool& commandPool,
                         vk::raii::Queue& transferQueue, VmaAllocator allocator)
    : m_Device(device), m_PhysicalDevice(physicalDevice),
      m_CommandPool(commandPool), m_TransferQueue(transferQueue),
      m_Allocator(allocator)
{
}

void ModelLoader::Init(vk::raii::Device& device,
                       vk::raii::PhysicalDevice& physicalDevice,
                       vk::raii::CommandPool& commandPool,
                       vk::raii::Queue& transferQueue, VmaAllocator allocator)
{
    if (s_Instance)
        throw std::runtime_error(
            "ModelLoader singleton is already initialised!");

    s_Instance = new ModelLoader(device, physicalDevice, commandPool,
                                 transferQueue, allocator);
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
    const aiScene* pScene = importer.ReadFile(
        path.data(), aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
                         aiProcess_CalcTangentSpace);

    if (!pScene)
        throw std::runtime_error(std::format("Failed to load model: {}", path));

    std::filesystem::path modelPath = path;
    std::string modelRoot = modelPath.parent_path().string() + "/";

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<std::unique_ptr<Material>> materials =
        LoadMaterials(pScene, modelRoot);

    ModelData* pModelData =
        new ModelData(path, std::move(materials), pScene->mNumMeshes);
    std::unique_ptr<Node> rootNode = std::make_unique<Node>();
    rootNode->ProcessNode(pModelData, pScene->mRootNode, pScene, glm::mat4(1.f),
                          vertices, indices);

    if (vertices.empty() || indices.empty())
        throw std::runtime_error(
            std::format("Model {} loaded with no vertices or indices!", path));

    vk::DeviceSize vertexBufSize = sizeof(vertices[0]) * vertices.size();
    AllocatedBuffer vertexBuffer = CreateStagedBuffer(
        m_Allocator, m_Device, m_CommandPool, m_TransferQueue, vertexBufSize,
        vk::BufferUsageFlagBits::eVertexBuffer, vertices.data());
    SetVkDebugName(m_Device, vertexBuffer.Buffer, vk::ObjectType::eBuffer,
                   std::format("{} Vertex Buffer", path).c_str());
    vmaSetAllocationName(m_Allocator, vertexBuffer.Allocation,
                         std::format("{} Vertex Buffer Memory", path).c_str());

    vk::DeviceSize indexBufSize = sizeof(indices[0]) * indices.size();
    AllocatedBuffer indexBuffer = CreateStagedBuffer(
        m_Allocator, m_Device, m_CommandPool, m_TransferQueue, indexBufSize,
        vk::BufferUsageFlagBits::eIndexBuffer, indices.data());
    SetVkDebugName(m_Device, indexBuffer.Buffer, vk::ObjectType::eBuffer,
                   std::format("{} Index Buffer", path).c_str());
    vmaSetAllocationName(m_Allocator, indexBuffer.Allocation,
                         std::format("{} Index Buffer Memory", path).c_str());

    pModelData->Init(std::move(vertexBuffer), std::move(indexBuffer),
                     std::move(rootNode));

    return pModelData;
}

std::vector<std::unique_ptr<Material>>
ModelLoader::LoadMaterials(const aiScene* pScene, const std::string& modelRoot)
{
    std::vector<std::unique_ptr<Material>> materials;
    for (size_t i = 0; i < pScene->mNumMaterials; i++)
    {
        aiMaterial* pMat = pScene->mMaterials[i];
        materials.emplace_back(
            MaterialFactory::Get()->CreatePBRMaterial(pMat, modelRoot));
    }
    return materials;
}
