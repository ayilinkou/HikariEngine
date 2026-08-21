#include "ModelLoader.h"

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include "MaterialFactory.h"
#include "ModelData.h"
#include "Node.h"
#include "Vertex.h"

#include <rhi/UniqueHandle.h>

#include <span>

ModelLoader::ModelLoader(Rhi::IDevice& rhiDevice, Rhi::IUploadContext& uploadContext)
    : m_RhiDevice(rhiDevice), m_UploadContext(uploadContext)
{
}

void ModelLoader::Init(Rhi::IDevice& rhiDevice, Rhi::IUploadContext& uploadContext)
{
    if (s_Instance)
        throw std::runtime_error("ModelLoader singleton is already initialised!");

    s_Instance = new ModelLoader(rhiDevice, uploadContext);
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

    const auto createUploaded = [&](Rhi::BufferUsage usage, auto& contents, const char* what)
    {
        Rhi::UniqueHandle<Rhi::BufferHandle> buffer(
            m_RhiDevice, m_RhiDevice.CreateBuffer(Rhi::BufferDesc{
                             .Size = std::span(contents).size_bytes(),
                             .Usage = usage | Rhi::BufferUsage::CopyDst,
                             .Access = Rhi::MemoryAccess::GpuOnly,
                             .DebugName = std::format("{} {} Buffer", path, what)}));

        m_UploadContext.UploadBuffer(buffer.Get(), 0u, std::as_bytes(std::span(contents)));
        return buffer;
    };

    Rhi::UniqueHandle<Rhi::BufferHandle> vertexBuffer =
        createUploaded(Rhi::BufferUsage::Vertex, vertices, "Vertex");
    Rhi::UniqueHandle<Rhi::BufferHandle> indexBuffer =
        createUploaded(Rhi::BufferUsage::Index, indices, "Index");

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
