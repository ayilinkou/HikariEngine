#pragma once

#include "glm/glm.hpp"

#include "vulkan/vulkan.hpp"

class Material;

struct InstanceData
{
    glm::mat4 Model;
};

struct MeshBatch
{
    uint32_t FirstInstance = 0u;
    uint32_t InstanceCount = 0u;
    uint32_t FirstIndex = 0u;
    uint32_t IndexOffset = 0u;
    uint32_t IndexCount = 0u;
    Material* pMaterial = nullptr;
    vk::Buffer IndexBuffer;
    vk::Buffer VertexBuffer;
};
