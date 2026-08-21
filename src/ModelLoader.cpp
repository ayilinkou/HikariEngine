#include "ModelLoader.h"

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include "MaterialFactory.h"
#include "ModelData.h"
#include "Node.h"
#include "Vertex.h"

#include <rhi/vulkan/BufferUtil.h>

ModelLoader::ModelLoader(Rhi::IDevice& rhiDevice, vk::raii::CommandPool& commandPool,
                         vk::raii::Queue& transferQueue)
    : m_RhiDevice(rhiDevice), m_CommandPool(commandPool), m_TransferQueue(transferQueue)
{
}

void ModelLoader::Init(Rhi::IDevice& rhiDevice, vk::raii::CommandPool& commandPool,
                       vk::raii::Queue& transferQueue)
{
    if (s_Instance)
        throw std::runtime_error("ModelLoader singleton is already initialised!");

    s_Instance = new ModelLoader(rhiDevice, commandPool, transferQueue);
}

void ModelLoader::Shutdown()
{
    if (!s_Instance)
        throw std::runtime_error("Attempting to shutdown ModelLoader when instance is null!");

    delete s_Instance;
    s_Instance = nullptr;
}

std::shared_ptr<ModelData> ModelLoader::Load(const std::string& path)
{
    Assimp::Importer importer;
    const aiScene* pScene =
        importer.ReadFile(path.data(), aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
                                           aiProcess_CalcTangentSpace);

    if (!pScene)
        throw std::runtime_error(std::format("Failed to load model: {}", path));

    std::filesystem::path modelPath = path;
    std::string modelRoot = modelPath.parent_path().string() + "/";

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<std::unique_ptr<Material>> materials = LoadMaterials(pScene, modelRoot);

    std::shared_ptr<ModelData> pModelData =
        std::make_shared<ModelData>(path, std::move(materials), pScene->mNumMeshes);
    std::unique_ptr<Node> rootNode = std::make_unique<Node>();
    rootNode->ProcessNode(pModelData.get(), pScene->mRootNode, pScene, glm::mat4(1.f), vertices,
                          indices);

    if (vertices.empty() || indices.empty())
        throw std::runtime_error(std::format("Model {} loaded with no vertices or indices!", path));

    vk::DeviceSize vertexBufSize = sizeof(vertices[0]) * vertices.size();
    Rhi::UniqueHandle<Rhi::BufferHandle> vertexBuffer(
        m_RhiDevice,
        Rhi::Vulkan::CreateStagedBuffer(m_RhiDevice, m_CommandPool, m_TransferQueue, vertexBufSize,
                                        Rhi::BufferUsage::Vertex, vertices.data(),
                                        std::format("{} Vertex Buffer", path)));

    vk::DeviceSize indexBufSize = sizeof(indices[0]) * indices.size();
    Rhi::UniqueHandle<Rhi::BufferHandle> indexBuffer(
        m_RhiDevice,
        Rhi::Vulkan::CreateStagedBuffer(m_RhiDevice, m_CommandPool, m_TransferQueue, indexBufSize,
                                        Rhi::BufferUsage::Index, indices.data(),
                                        std::format("{} Index Buffer", path)));

    pModelData->Init(std::move(vertexBuffer), std::move(indexBuffer), std::move(rootNode));

    return pModelData;
}

std::vector<std::unique_ptr<Material>> ModelLoader::LoadMaterials(const aiScene* pScene,
                                                                  const std::string& modelRoot)
{
    std::vector<std::unique_ptr<Material>> materials;
    for (size_t i = 0; i < pScene->mNumMaterials; i++)
    {
        aiMaterial* pMat = pScene->mMaterials[i];
        materials.emplace_back(MaterialFactory::Get()->CreatePBRMaterial(pMat, modelRoot));
    }
    return materials;
}
