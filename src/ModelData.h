#pragma once

#include <memory>
#include <string>

#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_raii.hpp"

#include "Material.h"

struct aiMaterial;

class ModelData
{
public:
    ModelData(vk::raii::Buffer vertexBuffer,
              vk::raii::DeviceMemory vertexMemory, vk::raii::Buffer indexBuffer,
              vk::raii::DeviceMemory indexMemory, Material* pMaterial,
              uint32_t indexCount, const std::string& path);

    vk::Buffer GetVertexBuffer() const { return *m_VertexBuffer; }
    vk::Buffer GetIndexBuffer() const { return *m_IndexBuffer; }

    uint32_t GetIndexCount() const { return m_IndexCount; }
    Material* GetMaterial() const { return m_Material.get(); }

    const std::string& GetFilepath() const { return m_Path; }

private:
    vk::raii::Buffer m_VertexBuffer = nullptr;
    vk::raii::Buffer m_IndexBuffer = nullptr;
    vk::raii::DeviceMemory m_VertexMemory = nullptr;
    vk::raii::DeviceMemory m_IndexMemory = nullptr;

    uint32_t m_IndexCount = 0u;

    std::unique_ptr<Material> m_Material = nullptr;

    const std::string m_Path;
};
