#include <string>
#include <vector>

#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_raii.hpp"

#include "Texture.h"
#include "Vertex.h"

enum TextureBinding : uint8_t
{
    Albedo,
    Normal,
    MetallicRoughness,

    COUNT
};

struct aiMaterial;

class Model
{
public:
    Model() {}

    void LoadModel(vk::raii::Device& device,
                   vk::raii::PhysicalDevice& physicalDevice,
                   vk::raii::CommandPool& commandPool,
                   vk::raii::Queue& transferQueue,
                   vk::raii::DescriptorPool& descriptorPool,
                   vk::raii::DescriptorSetLayout& materialSetLayout,
                   const vk::raii::Sampler& sampler, const std::string& path);

    const std::vector<Vertex>& GetVertices() const { return m_Vertices; }
    const std::vector<uint32_t>& GetIndices() const { return m_Indices; }

    vk::Buffer GetVertexBuffer() const { return *m_VertexBuffer; }
    vk::Buffer GetIndexBuffer() const { return *m_IndexBuffer; }

    vk::DescriptorSet GetDescriptorSet() const { return *m_DescriptorSet; }

private:
    void CreateVertexBuffer(vk::raii::Device& device,
                            vk::raii::PhysicalDevice& physicalDevice,
                            vk::raii::CommandPool& commandPool,
                            vk::raii::Queue& transferQueue);
    void CreateIndexBuffer(vk::raii::Device& device,
                           vk::raii::PhysicalDevice& physicalDevice,
                           vk::raii::CommandPool& commandPool,
                           vk::raii::Queue& transferQueue);
    void LoadTextures(vk::raii::Device& device,
                      vk::raii::PhysicalDevice& physicalDevice,
                      vk::raii::CommandPool& commandPool,
                      vk::raii::Queue& transferQueue, aiMaterial* mat);
    void CreateDescriptorSet(vk::raii::Device& device,
                             vk::raii::DescriptorPool& descriptorPool,
                             vk::raii::DescriptorSetLayout& materialSetLayout,
                             const vk::raii::Sampler& sampler);

private:
    vk::raii::Buffer m_VertexBuffer = nullptr;
    vk::raii::Buffer m_IndexBuffer = nullptr;
    vk::raii::DeviceMemory m_VertexMemory = nullptr;
    vk::raii::DeviceMemory m_IndexMemory = nullptr;

    std::vector<Vertex> m_Vertices;
    std::vector<uint32_t> m_Indices;

    std::unique_ptr<Texture> m_Albedo = nullptr;
    std::unique_ptr<Texture> m_Normal = nullptr;
    std::unique_ptr<Texture> m_MetallicRoughness = nullptr;

    vk::raii::DescriptorSet m_DescriptorSet = nullptr;

    std::string m_Name = "Name";
	std::string m_Path = "";
};
