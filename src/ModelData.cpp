#include "ModelData.h"

ModelData::ModelData(vk::raii::Buffer vertexBuffer,
                     vk::raii::DeviceMemory vertexMemory,
                     vk::raii::Buffer indexBuffer,
                     vk::raii::DeviceMemory indexMemory, Material* pMaterial,
                     uint32_t indexCount, const std::string& path)
    : m_VertexBuffer(std::move(vertexBuffer)),
      m_IndexBuffer(std::move(indexBuffer)),
      m_VertexMemory(std::move(vertexMemory)),
      m_IndexMemory(std::move(indexMemory)), m_IndexCount(indexCount),
      m_Material(pMaterial), m_Path(path)
{
}
