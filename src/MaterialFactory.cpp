#include "MaterialFactory.h"

#include <stdexcept>

MaterialFactory* MaterialFactory::s_Instance = nullptr;

MaterialFactory::MaterialFactory(vk::raii::Device& device,
                                 vk::raii::PhysicalDevice& physicalDevice,
                                 vk::raii::CommandPool& commandPool,
                                 vk::raii::Queue& transferQueue,
                                 vk::raii::DescriptorPool& descriptorPool,
                                 vk::raii::DescriptorSetLayout& setLayout,
                                 vk::raii::Sampler& sampler)
    : m_Device(device), m_PhysicalDevice(physicalDevice),
      m_CommandPool(commandPool), m_TransferQueue(transferQueue),
      m_DescriptorPool(descriptorPool), m_SetLayout(setLayout),
      m_Sampler(sampler)
{
}

void MaterialFactory::Init(vk::raii::Device& device,
                           vk::raii::PhysicalDevice& physicalDevice,
                           vk::raii::CommandPool& commandPool,
                           vk::raii::Queue& transferQueue,
                           vk::raii::DescriptorPool& descriptorPool,
                           vk::raii::DescriptorSetLayout& setLayout,
                           vk::raii::Sampler& sampler)
{
    if (s_Instance)
        throw std::runtime_error("MaterialFactory is already initialised!");
    s_Instance =
        new MaterialFactory(device, physicalDevice, commandPool, transferQueue,
                            descriptorPool, setLayout, sampler);
}

void MaterialFactory::Shutdown()
{
    if (s_Instance)
    {
        delete s_Instance;
        s_Instance = nullptr;
    }
}

std::unique_ptr<PBRMaterial>
MaterialFactory::CreatePBRMaterial(aiMaterial* mat,
                                   const std::string& texturesParentFolder)
{
    return std::make_unique<PBRMaterial>(
        m_Device, m_PhysicalDevice, m_CommandPool, m_TransferQueue,
        m_DescriptorPool, m_SetLayout, m_Sampler, mat, texturesParentFolder);
}
