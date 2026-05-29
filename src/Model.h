#include <string>
#include <vector>
#include <memory>

#include "PBRMaterial.h"
#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_raii.hpp"

#include "Vertex.h"

struct aiMaterial;

class Model
{
public:
    Model() {}

    void LoadModel(vk::raii::Device& device,
                   vk::raii::PhysicalDevice& physicalDevice,
                   vk::raii::CommandPool& commandPool,
                   vk::raii::Queue& transferQueue,
                   const std::string& path);

    const std::vector<Vertex>& GetVertices() const { return m_Vertices; }
    const std::vector<uint32_t>& GetIndices() const { return m_Indices; }

    vk::Buffer GetVertexBuffer() const { return *m_VertexBuffer; }
    vk::Buffer GetIndexBuffer() const { return *m_IndexBuffer; }

	Material* GetMaterial() const { return m_Material.get(); }

private:
    void CreateVertexBuffer(vk::raii::Device& device,
                            vk::raii::PhysicalDevice& physicalDevice,
                            vk::raii::CommandPool& commandPool,
                            vk::raii::Queue& transferQueue);
    void CreateIndexBuffer(vk::raii::Device& device,
                           vk::raii::PhysicalDevice& physicalDevice,
                           vk::raii::CommandPool& commandPool,
                           vk::raii::Queue& transferQueue);

private:
    vk::raii::Buffer m_VertexBuffer = nullptr;
    vk::raii::Buffer m_IndexBuffer = nullptr;
    vk::raii::DeviceMemory m_VertexMemory = nullptr;
    vk::raii::DeviceMemory m_IndexMemory = nullptr;

    std::vector<Vertex> m_Vertices;
    std::vector<uint32_t> m_Indices;

	std::unique_ptr<PBRMaterial> m_Material = nullptr; // TODO: change to Material

    std::string m_Name = "Name";
	std::string m_Path = "";
};
