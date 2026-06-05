#include "Model.h"

#include "ModelData.h"
#include "ResourceManager.h"

Model::Model(const std::string& path)
    : m_pModelData(ResourceManager::Get()->LoadModel(path))
{
}

Model::~Model()
{
    ResourceManager::Get()->UnloadModel(m_pModelData->GetFilepath());
}

vk::Buffer Model::GetVertexBuffer() const
{
    return m_pModelData->GetVertexBuffer();
}
vk::Buffer Model::GetIndexBuffer() const
{
    return m_pModelData->GetIndexBuffer();
}

uint32_t Model::GetIndexCount() const { return m_pModelData->GetIndexCount(); }
Material* Model::GetMaterial() const { return m_pModelData->GetMaterial(); }

MeshBatch Model::GetMeshBatch() const
{
    return {.InstanceCount = 1u,
            .IndexCount = GetIndexCount(),
            .pMaterial = GetMaterial(),
            .IndexBuffer = GetIndexBuffer(),
            .VertexBuffer = GetVertexBuffer()};
}
