#include "CloudSystem.h"
#include "Log.h"
#include "Utility.h"

inline constexpr LogCategory LogCloudSystem{"Cloud System"};

const uint32_t CloudSystem::s_NOISE_RES = 128u;

CloudSystem::CloudSystem(CloudSystemCreateInfo createInfo)
    : m_Device(createInfo.Device), m_PhysicalDevice(createInfo.PhysicalDevice),
      m_FramesInFlight(createInfo.FramesInFlight)
{
    Init(createInfo);
}

void CloudSystem::Init(const CloudSystemCreateInfo& createInfo)
{
    LogMsg(LogSeverity::Info, LogCloudSystem, "Init()");

	CreateTextureSampler();
    CreateOutputImages(createInfo.SwapchainWidth, createInfo.SwapchainHeight);
    CreateNoiseTexture();
    CreateDescriptorPool();
    CreateDescriptorSetLayout();
    CreatePipelineLayout(createInfo.GlobalSetLayout, createInfo.DepthSetLayout);
    CreatePipeline();
    AllocateDescriptorSets();
    WriteDescriptorSets();

    CreateBakeDescriptorPool();
    CreateBakeDescriptorSetLayout();
    CreateBakePipelineLayout();
    CreateBakePipeline();
    AllocateAndWriteBakeDescriptorSet();
    BakeNoiseTexture(createInfo.CommandPool, createInfo.ComputeQueue);
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
    std::array<vk::DescriptorSetLayoutBinding, 2> bindings{
        {{0, vk::DescriptorType::eStorageImage, 1,
          vk::ShaderStageFlagBits::eCompute},
         {1, vk::DescriptorType::eCombinedImageSampler, 1,
          vk::ShaderStageFlagBits::eCompute}}};

    vk::DescriptorSetLayoutCreateInfo layoutInfo{
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings = bindings.data(),
    };
    m_SetLayout = vk::raii::DescriptorSetLayout(m_Device, layoutInfo);
}

void CloudSystem::CreateBakeDescriptorSetLayout()
{
    std::array<vk::DescriptorSetLayoutBinding, 1> bindings{{
        {0, vk::DescriptorType::eStorageImage, 1,
         vk::ShaderStageFlagBits::eCompute},
    }};

    vk::DescriptorSetLayoutCreateInfo layoutInfo{
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings = bindings.data(),
    };
    m_BakeSetLayout = vk::raii::DescriptorSetLayout(m_Device, layoutInfo);
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

void CloudSystem::CreateBakePipelineLayout()
{
    vk::PushConstantRange pushRange{
        .stageFlags = vk::ShaderStageFlagBits::eCompute,
        .offset = 0,
        .size = sizeof(BakeConstants),
    };

    vk::PipelineLayoutCreateInfo layoutInfo{
        .setLayoutCount = 1,
        .pSetLayouts = &*m_BakeSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange,
    };
    m_BakePipelineLayout = vk::raii::PipelineLayout(m_Device, layoutInfo);
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

void CloudSystem::CreateBakePipeline()
{
    auto code = ReadFile("shaders/bakePerlinWorley.comp.spv");

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
        .layout = *m_BakePipelineLayout,
    };
    m_BakePipeline = vk::raii::Pipeline(m_Device, nullptr, pipelineInfo);
}

void CloudSystem::CreateDescriptorPool()
{
    std::array<vk::DescriptorPoolSize, 2> poolSizes{
        {{vk::DescriptorType::eStorageImage, m_FramesInFlight},
         {vk::DescriptorType::eCombinedImageSampler, m_FramesInFlight}}};

    vk::DescriptorPoolCreateInfo poolInfo{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = m_FramesInFlight,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
    };
    m_DescriptorPool = vk::raii::DescriptorPool(m_Device, poolInfo);
}

void CloudSystem::CreateBakeDescriptorPool()
{
    std::array<vk::DescriptorPoolSize, 1> poolSizes{{
        {vk::DescriptorType::eStorageImage, 1},
    }};

    vk::DescriptorPoolCreateInfo poolInfo{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = 1,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
    };
    m_BakeDescriptorPool = vk::raii::DescriptorPool(m_Device, poolInfo);
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

void CloudSystem::AllocateAndWriteBakeDescriptorSet()
{
    vk::DescriptorSetAllocateInfo allocInfo{
        .descriptorPool = *m_BakeDescriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &*m_BakeSetLayout,
    };
    m_BakeDescriptorSet =
        std::move(vk::raii::DescriptorSets(m_Device, allocInfo).front());

    vk::DescriptorImageInfo noiseImageInfo{
        .imageView = *m_PerlinWorleyView,
        .imageLayout = vk::ImageLayout::eGeneral,
    };

    vk::WriteDescriptorSet writeDescSet{
        .dstSet = *m_BakeDescriptorSet,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eStorageImage,
        .pImageInfo = &noiseImageInfo,
    };
    m_Device.updateDescriptorSets(writeDescSet, {});
}

void CloudSystem::WriteDescriptorSets()
{
    for (uint32_t i = 0u; i < m_FramesInFlight; i++)
    {
        vk::DescriptorImageInfo storageImageInfo{
            .imageView = *m_OutputViews[i],
            .imageLayout = vk::ImageLayout::eGeneral,
        };
        vk::DescriptorImageInfo perlinWorleyImageInfo{
            .sampler = *m_TextureSampler,
            .imageView = *m_PerlinWorleyView,
			.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        };

        std::array<vk::WriteDescriptorSet, 2> writeDescSet{
            vk::WriteDescriptorSet{.dstSet = *m_DescriptorSets[i],
                                   .dstBinding = 0,
                                   .dstArrayElement = 0,
                                   .descriptorCount = 1,
                                   .descriptorType =
                                       vk::DescriptorType::eStorageImage,
                                   .pImageInfo = &storageImageInfo},
            vk::WriteDescriptorSet{
                .dstSet = *m_DescriptorSets[i],
                .dstBinding = 1,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .pImageInfo = &perlinWorleyImageInfo,
            }};

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

void CloudSystem::CreateNoiseTexture()
{
    vk::ImageCreateInfo imageInfo{
        .imageType = vk::ImageType::e3D,
        .format = vk::Format::eR8Unorm,
        .extent = {s_NOISE_RES, s_NOISE_RES, s_NOISE_RES},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage =
            vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined,
    };
    m_PerlinWorleyImage = vk::raii::Image(m_Device, imageInfo);
    SetVkDebugName(m_Device, *m_PerlinWorleyImage, vk::ObjectType::eImage,
                   "Perlin Worley Image");

    vk::MemoryRequirements memReq = m_PerlinWorleyImage.getMemoryRequirements();
    uint32_t memTypeIndex =
        FindMemoryType(m_PhysicalDevice, memReq.memoryTypeBits,
                       vk::MemoryPropertyFlagBits::eDeviceLocal);
    vk::MemoryAllocateInfo allocInfo{.allocationSize = memReq.size,
                                     .memoryTypeIndex = memTypeIndex};
    m_PerlinWorleyMemory = vk::raii::DeviceMemory(m_Device, allocInfo);
    SetVkDebugName(m_Device, *m_PerlinWorleyMemory,
                   vk::ObjectType::eDeviceMemory, "Perlin Worley Image Memory");
    m_PerlinWorleyImage.bindMemory(*m_PerlinWorleyMemory, 0);

    vk::ImageViewCreateInfo viewInfo{
        .image = *m_PerlinWorleyImage,
        .viewType = vk::ImageViewType::e3D,
        .format = vk::Format::eR8Unorm,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };
    m_PerlinWorleyView = vk::raii::ImageView(m_Device, viewInfo);
    SetVkDebugName(m_Device, *m_PerlinWorleyView, vk::ObjectType::eImageView,
                   "Perlin Worley Image View");
}

void CloudSystem::BakeNoiseTexture(vk::raii::CommandPool& commandPool,
                                   vk::raii::Queue& computeQueue)
{
    LogMsg(LogSeverity::Info, LogCloudSystem,
           "Baking perlin worley texture ({}x{}x{})", s_NOISE_RES, s_NOISE_RES,
           s_NOISE_RES);

    vk::raii::CommandBuffer cmd = BeginSingleTimeCommand(m_Device, commandPool);

    vk::ImageMemoryBarrier2 toGeneral{
        .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
        .srcAccessMask = vk::AccessFlagBits2::eNone,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eGeneral,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = *m_PerlinWorleyImage,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };
    vk::DependencyInfo dep1{.imageMemoryBarrierCount = 1,
                            .pImageMemoryBarriers = &toGeneral};
    cmd.pipelineBarrier2(dep1);

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *m_BakePipeline);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                           *m_BakePipelineLayout, 0, *m_BakeDescriptorSet, {});

    BakeConstants bc{.Resolution = s_NOISE_RES, .WorleyPointsPerCell = 1};
    cmd.pushConstants<BakeConstants>(*m_BakePipelineLayout,
                                     vk::ShaderStageFlagBits::eCompute, 0, bc);

    cmd.dispatch(s_NOISE_RES / 4, s_NOISE_RES / 4,
                 s_NOISE_RES / 4); // matches numthreads(4,4,4)

    vk::ImageMemoryBarrier2 toRead{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
        .oldLayout = vk::ImageLayout::eGeneral,
        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = *m_PerlinWorleyImage,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };
    vk::DependencyInfo dep2{.imageMemoryBarrierCount = 1,
                            .pImageMemoryBarriers = &toRead};
    cmd.pipelineBarrier2(dep2);

    // TODO: move to a read only image
    EndSingleTimeCommand(cmd, computeQueue);
}

void CloudSystem::CreateTextureSampler()
{
    LogMsg(LogSeverity::Info, LogCloudSystem, "CreateTextureSampler()");

    vk::SamplerCreateInfo createInfo{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
        .addressModeU = vk::SamplerAddressMode::eRepeat,
        .addressModeV = vk::SamplerAddressMode::eRepeat,
        .addressModeW = vk::SamplerAddressMode::eRepeat,
        .anisotropyEnable = vk::False,
        .compareEnable = vk::False,
        .minLod = 0.f,
        .maxLod = 0.f,
        .borderColor = vk::BorderColor::eIntOpaqueBlack,
        .unnormalizedCoordinates = vk::False};
    m_TextureSampler = vk::raii::Sampler(m_Device, createInfo);
    SetVkDebugName(m_Device, *m_TextureSampler, vk::ObjectType::eSampler,
                   "Cloud System Texture Sampler");
}
