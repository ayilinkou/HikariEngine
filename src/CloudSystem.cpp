#include "CloudSystem.h"
#include "Utility.h"
#include "vulkan/vulkan.hpp"

CloudSystem::CloudSystem(CloudSystemCreateInfo createInfo)
    : m_Device(createInfo.Device), m_PhysicalDevice(createInfo.PhysicalDevice),
      m_FramesInFlight(createInfo.FramesInFlight)
{
    Init(createInfo.SwapchainWidth, createInfo.SwapchainHeight, createInfo.GlobalSetLayout,
         createInfo.DepthSetLayout);
}

void CloudSystem::Init(uint32_t width, uint32_t height,
                       vk::raii::DescriptorSetLayout& globalSetLayout,
                       vk::raii::DescriptorSetLayout& depthSetLayout)
{
    CreateOutputImages(width, height);
    CreateDescriptorPool();
    CreateDescriptorSetLayout();
    CreatePipelineLayout(globalSetLayout, depthSetLayout);
    CreatePipeline();
    AllocateDescriptorSets();
    WriteDescriptorSets();
}

void CloudSystem::Resize(uint32_t width, uint32_t height)
{
    CreateOutputImages(width, height);
    WriteDescriptorSets();
}

void CloudSystem::CreateOutputImages(uint32_t width, uint32_t height)
{
    m_OutputImages.clear();
    m_OutputImageMemory.clear();
    m_OutputViews.clear();

    m_Width = width;
    m_Height = height;

	const uint32_t resFactor = 4u;

    m_OutputWidth = std::max(1u, width / resFactor);
    m_OutputHeight = std::max(1u, height / resFactor);

    for (uint32_t i = 0; i < m_FramesInFlight; ++i)
    {
        vk::ImageCreateInfo imageInfo{
            .imageType = vk::ImageType::e2D,
            .format = vk::Format::eR16G16B16A16Sfloat,
            .extent = {m_OutputWidth, m_OutputHeight, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eStorage |
                     vk::ImageUsageFlagBits::eSampled,
            .sharingMode = vk::SharingMode::eExclusive,
            .initialLayout = vk::ImageLayout::eUndefined,
        };
        m_OutputImages.emplace_back(m_Device, imageInfo);

        vk::MemoryRequirements memReq =
            m_OutputImages.back().getMemoryRequirements();
        uint32_t memTypeIndex =
            FindMemoryType(m_PhysicalDevice, memReq.memoryTypeBits,
                           vk::MemoryPropertyFlagBits::eDeviceLocal);

        vk::MemoryAllocateInfo allocInfo{
            .allocationSize = memReq.size,
            .memoryTypeIndex = memTypeIndex,
        };
        m_OutputImageMemory.emplace_back(m_Device, allocInfo);
        m_OutputImages.back().bindMemory(*m_OutputImageMemory.back(), 0);

        vk::ImageViewCreateInfo viewInfo{
            .image = *m_OutputImages.back(),
            .viewType = vk::ImageViewType::e2D,
            .format = vk::Format::eR16G16B16A16Sfloat,
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        m_OutputViews.emplace_back(m_Device, viewInfo);
    }
}

void CloudSystem::CreateDescriptorSetLayout()
{
    std::array<vk::DescriptorSetLayoutBinding, 1> bindings{{
        {0, vk::DescriptorType::eStorageImage, 1,
         vk::ShaderStageFlagBits::eCompute},
    }};

    vk::DescriptorSetLayoutCreateInfo layoutInfo{
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings = bindings.data(),
    };
    m_SetLayout = vk::raii::DescriptorSetLayout(m_Device, layoutInfo);
}

void CloudSystem::CreatePipelineLayout(
    vk::raii::DescriptorSetLayout& globalSetLayout,
    vk::raii::DescriptorSetLayout& depthSetLayout)
{
    std::array<vk::DescriptorSetLayout, 3> setLayouts = {
        *globalSetLayout,
        *depthSetLayout,
        *m_SetLayout,
    };

    vk::PushConstantRange pushRange{
        .stageFlags = vk::ShaderStageFlagBits::eCompute,
        .offset = 0,
        .size = sizeof(CloudPushConstants),
    };

    vk::PipelineLayoutCreateInfo layoutInfo{
        .setLayoutCount = static_cast<uint32_t>(setLayouts.size()),
        .pSetLayouts = setLayouts.data(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange,
    };
    m_PipelineLayout = vk::raii::PipelineLayout(m_Device, layoutInfo);
}

void CloudSystem::CreatePipeline()
{
    auto code =
        ReadFile("shaders/clouds.comp.spv"); // your existing SPIR-V loader

    vk::ShaderModuleCreateInfo moduleInfo{
        .codeSize = code.size(),
        .pCode = reinterpret_cast<const uint32_t*>(code.data()),
    };
    vk::raii::ShaderModule shaderModule(m_Device, moduleInfo);

    vk::PipelineShaderStageCreateInfo stageInfo{
        .stage = vk::ShaderStageFlagBits::eCompute,
        .module = *shaderModule,
        .pName = "main",
    };

    vk::ComputePipelineCreateInfo pipelineInfo{
        .stage = stageInfo,
        .layout = *m_PipelineLayout,
    };
    m_Pipeline = vk::raii::Pipeline(m_Device, nullptr, pipelineInfo);
}

void CloudSystem::CreateDescriptorPool()
{
    std::array<vk::DescriptorPoolSize, 1> poolSizes{
        {{vk::DescriptorType::eStorageImage, m_FramesInFlight}}};

    vk::DescriptorPoolCreateInfo poolInfo{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = m_FramesInFlight,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
    };
    m_DescriptorPool = vk::raii::DescriptorPool(m_Device, poolInfo);
}

void CloudSystem::AllocateDescriptorSets()
{
    std::vector<vk::DescriptorSetLayout> layouts(m_FramesInFlight,
                                                 *m_SetLayout);
    vk::DescriptorSetAllocateInfo allocInfo{
        .descriptorPool = *m_DescriptorPool,
        .descriptorSetCount = m_FramesInFlight,
        .pSetLayouts = layouts.data(),
    };
    m_DescriptorSets = vk::raii::DescriptorSets(m_Device, allocInfo);
}

void CloudSystem::WriteDescriptorSets()
{
    for (uint32_t i = 0u; i < m_FramesInFlight; i++)
    {
        vk::DescriptorImageInfo storageImageInfo{
            .imageView = *m_OutputViews[i],
            .imageLayout = vk::ImageLayout::eGeneral,
        };

        vk::WriteDescriptorSet writeDescSet{
            .dstSet = *m_DescriptorSets[i],
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eStorageImage,
            .pImageInfo = &storageImageInfo};

        m_Device.updateDescriptorSets(writeDescSet, {});
    }
}

void CloudSystem::RecordDispatch(vk::raii::CommandBuffer& cmd,
                                 uint32_t frameIndex,
                                 vk::raii::DescriptorSet& globalSet,
                                 vk::raii::DescriptorSet& depthSet)
{
    cmd.begin({});

    vk::ImageMemoryBarrier2 toGeneral{
        .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
        .srcAccessMask = vk::AccessFlagBits2::eNone,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eGeneral,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = *m_OutputImages[frameIndex],
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };

    vk::DependencyInfo toGeneralDependencyInfo{
        .imageMemoryBarrierCount = 1u, .pImageMemoryBarriers = &toGeneral};
    cmd.pipelineBarrier2(toGeneralDependencyInfo);

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *m_Pipeline);
    std::array<vk::DescriptorSet, 3> sets = {*globalSet, *depthSet,
                                             *m_DescriptorSets[frameIndex]};
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *m_PipelineLayout,
                           0, sets, {});

    cmd.pushConstants<CloudPushConstants>(
        *m_PipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, m_CloudData);

    cmd.dispatch((m_OutputWidth + 7) / 8, (m_OutputHeight + 7) / 8, 1);

    vk::ImageMemoryBarrier2 toRead{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
        .oldLayout = vk::ImageLayout::eGeneral,
        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = *m_OutputImages[frameIndex],
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };

    vk::DependencyInfo toReadDependencyInfo{.imageMemoryBarrierCount = 1u,
                                            .pImageMemoryBarriers = &toRead};
    cmd.pipelineBarrier2(toReadDependencyInfo);

    cmd.end();
}
