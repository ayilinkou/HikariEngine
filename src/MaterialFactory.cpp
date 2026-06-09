#include "MaterialFactory.h"

#include "Utility.h"

MaterialFactory* MaterialFactory::s_Instance = nullptr;
const uint8_t MaterialFactory::s_MAX_TEXTURE_COUNT_PER_MAT = 3u;
const uint16_t MaterialFactory::s_MAX_MATERIAL_SET_COUNT = 5u;

MaterialFactory::MaterialFactory(vk::raii::Device& device,
                                 vk::raii::Sampler& sampler)
    : m_Device(device), m_Sampler(sampler)
{
    CreateDescriptorPool();
    CreateDescriptorSetLayout();
}

void MaterialFactory::Init(vk::raii::Device& device,
                           vk::raii::Sampler& sampler)
{
    if (s_Instance)
        throw std::runtime_error(
            "MaterialFactory singleton is already initialised!");
    s_Instance = new MaterialFactory(device, sampler);
}

void MaterialFactory::Shutdown()
{
    if (!s_Instance)
        throw std::runtime_error(
            "Attempting to shutdown MaterialFactory when it is already null!");

    delete s_Instance;
    s_Instance = nullptr;
}

void MaterialFactory::CreateDescriptorPool()
{
    std::array materialPoolSize = {
        vk::DescriptorPoolSize{.type =
                                   vk::DescriptorType::eCombinedImageSampler,
                               .descriptorCount = s_MAX_TEXTURE_COUNT_PER_MAT *
                                                  s_MAX_MATERIAL_SET_COUNT},
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eUniformBuffer,
                               .descriptorCount = s_MAX_MATERIAL_SET_COUNT}};
    vk::DescriptorPoolCreateInfo matCreateInfo{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = s_MAX_MATERIAL_SET_COUNT,
        .poolSizeCount = materialPoolSize.size(),
        .pPoolSizes = materialPoolSize.data()};

    m_DescriptorPool = vk::raii::DescriptorPool(m_Device, matCreateInfo);
    SetVkDebugName(m_Device, *m_DescriptorPool, vk::ObjectType::eDescriptorPool,
                   "Material Factory Descriptor Pool");
}

void MaterialFactory::CreateDescriptorSetLayout()
{
    std::array matBindings = {
        vk::DescriptorSetLayoutBinding(
            TextureBinding::Albedo, vk::DescriptorType::eCombinedImageSampler,
            1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(
            TextureBinding::Normal, vk::DescriptorType::eCombinedImageSampler,
            1, vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(
            TextureBinding::MetallicRoughness,
            vk::DescriptorType::eCombinedImageSampler, 1,
            vk::ShaderStageFlagBits::eFragment),
        vk::DescriptorSetLayoutBinding(
            TextureBinding::COUNT, // TODO: using TextureBinding::COUNT feels
                                   // bad here
            vk::DescriptorType::eUniformBuffer, 1,
            vk::ShaderStageFlagBits::eFragment)};

    std::array<vk::DescriptorBindingFlags, 4> bindingFlags = {
        vk::DescriptorBindingFlagBits::ePartiallyBound,
        vk::DescriptorBindingFlagBits::ePartiallyBound,
        vk::DescriptorBindingFlagBits::ePartiallyBound,
        {}};

    vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{
        .bindingCount = bindingFlags.size(),
        .pBindingFlags = bindingFlags.data()};

    vk::DescriptorSetLayoutCreateInfo matCreateInfo{
        .pNext = &flagsInfo,
        .bindingCount = matBindings.size(),
        .pBindings = matBindings.data()};

    m_SetLayout = vk::raii::DescriptorSetLayout(m_Device, matCreateInfo);
    SetVkDebugName(m_Device, *m_SetLayout, vk::ObjectType::eDescriptorSetLayout,
                   "Material Factory Descriptor Set Layout");
}

PBRMaterial*
MaterialFactory::CreatePBRMaterial(aiMaterial* mat,
                                   const std::string& texturesParentFolder)
{
    return new PBRMaterial(m_Device, m_DescriptorPool,
                           m_SetLayout, m_Sampler, mat, texturesParentFolder);
}
