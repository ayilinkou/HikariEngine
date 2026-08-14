#pragma once

#include "AllocatedBuffer.h"
#include "Drawable.h"
#include "Material.h"
#include "Mesh.h"
#include "Node.h"

struct aiMaterial;

class ModelData
{
public:
    ModelData(const std::string& path, std::vector<std::unique_ptr<Material>> materials,
              uint32_t meshCount);
    void Init(AllocatedBuffer vertexBuffer, AllocatedBuffer indexBuffer,
              std::unique_ptr<Node> rootNode);

    Mesh* RegisterMesh(aiMesh* mesh, uint32_t meshIndex, const glm::mat4& localTransform,
                       std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);

    vk::Buffer GetVertexBuffer() const { return m_VertexBuffer.Buffer; }
    vk::Buffer GetIndexBuffer() const { return m_IndexBuffer.Buffer; }

    const std::vector<Drawable>& GetDrawables() const { return m_Drawables; }

    const std::string& GetFilepath() const { return m_Path; }

private:
    std::vector<Mesh> m_Meshes;
    std::vector<std::unique_ptr<Material>> m_Materials;
    std::unordered_map<uint32_t, std::vector<glm::mat4>> m_MeshLocalTransforms;

    std::unique_ptr<Node> m_RootNode = nullptr;

    AllocatedBuffer m_VertexBuffer;
    AllocatedBuffer m_IndexBuffer;

    std::vector<Drawable> m_Drawables;

    const std::string m_Path;
};
