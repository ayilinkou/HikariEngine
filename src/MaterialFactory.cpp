#include "MaterialFactory.h"

#include <stdexcept>

MaterialFactory* MaterialFactory::s_Instance = nullptr;
const uint8_t MaterialFactory::s_MAX_TEXTURE_COUNT_PER_MAT = 5u;
const uint16_t MaterialFactory::s_MAX_MATERIAL_SET_COUNT = 2u;

MaterialFactory::MaterialFactory(vk::raii::Device& device,
                                 vk::raii::PhysicalDevice& physicalDevice,
                                 vk::raii::CommandPool& commandPool,
                                 vk::raii::Queue& transferQueue,
                                 vk::raii::Sampler& sampler)
    : m_Device(device), m_PhysicalDevice(physicalDevice),
      m_CommandPool(commandPool), m_TransferQueue(transferQueue),
      m_Sampler(sampler)
{
    CreateDescriptorPool();
    CreateDescriptorSetLayout();
}

void MaterialFactory::Init(vk::raii::Device& device,
                           vk::raii::PhysicalDevice& physicalDevice,
                           vk::raii::CommandPool& commandPool,
                           vk::raii::Queue& transferQueue,
                           vk::raii::Sampler& sampler)
{
    if (s_Instance)
        throw std::runtime_error("MaterialFactory is already initialised!");
    s_Instance = new MaterialFactory(device, physicalDevice, commandPool,
                                     transferQueue, sampler);
}

void MaterialFactory::Shutdown()
{
    if (s_Instance)
    {
        delete s_Instance;
        s_Instance = nullptr;
    }
}

void MaterialFactory::CreateDescriptorPool()
{
    std::array materialPoolSize = {vk::DescriptorPoolSize{
        .type = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = s_MAX_TEXTURE_COUNT_PER_MAT}};
    vk::DescriptorPoolCreateInfo matCreateInfo{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = s_MAX_MATERIAL_SET_COUNT,
        .poolSizeCount = materialPoolSize.size(),
        .pPoolSizes = materialPoolSize.data()};

    m_DescriptorPool = vk::raii::DescriptorPool(m_Device, matCreateInfo);
}

void MaterialFactory::CreateDescriptorSetLayout()
{
    std::array matBindings = {vk::DescriptorSetLayoutBinding(
                                  0, vk::DescriptorType::eCombinedImageSampler,
                                  1, vk::ShaderStageFlagBits::eFragment),
                              vk::DescriptorSetLayoutBinding(
                                  1, vk::DescriptorType::eCombinedImageSampler,
                                  1, vk::ShaderStageFlagBits::eFragment),
                              vk::DescriptorSetLayoutBinding(
                                  2, vk::DescriptorType::eCombinedImageSampler,
                                  1, vk::ShaderStageFlagBits::eFragment),
                              vk::DescriptorSetLayoutBinding(
                                  3, vk::DescriptorType::eCombinedImageSampler,
                                  1, vk::ShaderStageFlagBits::eFragment),
                              vk::DescriptorSetLayoutBinding(
                                  4, vk::DescriptorType::eCombinedImageSampler,
                                  1, vk::ShaderStageFlagBits::eFragment)};
    vk::DescriptorSetLayoutCreateInfo matCreateInfo{
        .bindingCount = matBindings.size(), .pBindings = matBindings.data()};
    m_SetLayout = vk::raii::DescriptorSetLayout(m_Device, matCreateInfo);
}

PBRMaterial*
MaterialFactory::CreatePBRMaterial(aiMaterial* mat,
                                   const std::string& texturesParentFolder)
{
    return new PBRMaterial(m_Device, m_PhysicalDevice, m_CommandPool,
                           m_TransferQueue, m_DescriptorPool, m_SetLayout,
                           m_Sampler, mat, texturesParentFolder);
}
