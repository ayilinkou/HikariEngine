#pragma once

#include <memory>

#include "vulkan/vulkan.hpp"

#include "Material.h"
#include "PBRMaterial.h"

struct aiMaterial;

class MaterialFactory
{
public:
    static void
    Init(vk::raii::Device& device, vk::raii::PhysicalDevice& physicalDevice,
         vk::raii::CommandPool& commandPool, vk::raii::Queue& transferQueue,
         vk::raii::DescriptorPool& descriptorPool,
         vk::raii::DescriptorSetLayout& setLayout, vk::raii::Sampler& sampler);
    static void Shutdown();

    static MaterialFactory* Get() { return s_Instance; }

    [[nodiscard]] std::unique_ptr<PBRMaterial> CreatePBRMaterial(aiMaterial* mat,
                                   const std::string& texturesParentFolder);

private:
    MaterialFactory(vk::raii::Device& device,
                    vk::raii::PhysicalDevice& physicalDevice,
                    vk::raii::CommandPool& commandPool,
                    vk::raii::Queue& transferQueue,
                    vk::raii::DescriptorPool& descriptorPool,
                    vk::raii::DescriptorSetLayout& setLayout,
                    vk::raii::Sampler& sampler);

private:
    static MaterialFactory* s_Instance;

    vk::raii::Device& m_Device;
    vk::raii::PhysicalDevice& m_PhysicalDevice;
    vk::raii::CommandPool& m_CommandPool;
    vk::raii::Queue& m_TransferQueue;
    vk::raii::DescriptorPool& m_DescriptorPool;
    vk::raii::DescriptorSetLayout& m_SetLayout;
    vk::raii::Sampler& m_Sampler;
};
