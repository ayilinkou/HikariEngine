#pragma once

struct CloudSystemCreateInfo
{
    vk::raii::Device& Device;
    vk::raii::PhysicalDevice& PhysicalDevice;
    vk::raii::DescriptorSetLayout& GlobalSetLayout;
    vk::raii::DescriptorSetLayout& DepthSetLayout;
    uint32_t SwapchainWidth;
    uint32_t SwapchainHeight;
    uint32_t FramesInFlight;
};

class CloudSystem
{
private:
    struct CloudPushConstants
    {
        glm::vec3 WindVelocity = {0.05f, 0.f, 0.03f};
        float MinHeight = 1500.f;
        float MaxHeight = 4000.f;
        float Coverage = 0.5f;
        uint32_t StepCount = 64u;
    };

public:
    CloudSystem(CloudSystemCreateInfo createInfo);

    void RecordDispatch(vk::raii::CommandBuffer& cmd, uint32_t frameIndex,
                        vk::raii::DescriptorSet& globalSet,
                        vk::raii::DescriptorSet& depthSet);
    void Resize(uint32_t width, uint32_t height);

    vk::raii::ImageView& GetImageView(uint8_t frameIndex)
    {
        return m_OutputViews[frameIndex];
    }

private:
    void Init(uint32_t width, uint32_t height,
              vk::raii::DescriptorSetLayout& globalSetLayout,
              vk::raii::DescriptorSetLayout& depthSetLayout);

    void CreateOutputImages(uint32_t width, uint32_t height);
    void CreateDescriptorSetLayout();
    void CreatePipelineLayout(vk::raii::DescriptorSetLayout& globalSetLayout,
                              vk::raii::DescriptorSetLayout& depthSetLayout);
    void CreatePipeline();
    void CreateDescriptorPool();
    void AllocateDescriptorSets();
    void WriteDescriptorSets();

private:
    vk::raii::Device& m_Device;
    vk::raii::PhysicalDevice& m_PhysicalDevice;

    vk::raii::DescriptorSetLayout m_SetLayout = nullptr;
    vk::raii::DescriptorPool m_DescriptorPool = nullptr;
    vk::raii::PipelineLayout m_PipelineLayout = nullptr;
    vk::raii::Pipeline m_Pipeline = nullptr;
    std::vector<vk::raii::DescriptorSet> m_DescriptorSets;

    std::vector<vk::raii::Image> m_OutputImages;
    std::vector<vk::raii::DeviceMemory> m_OutputImageMemory;
    std::vector<vk::raii::ImageView> m_OutputViews;

    const uint32_t m_FramesInFlight;
    uint32_t m_Width;
    uint32_t m_Height;
    uint32_t m_OutputWidth;
    uint32_t m_OutputHeight;

    CloudPushConstants m_CloudData{};
};
