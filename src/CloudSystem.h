#pragma once

struct CloudSystemCreateInfo
{
    vk::raii::Device& Device;
    vk::raii::PhysicalDevice& PhysicalDevice;
    vk::raii::DescriptorSetLayout& GlobalSetLayout;
    vk::raii::DescriptorSetLayout& DepthSetLayout;
    vk::raii::CommandPool& CommandPool;
    vk::raii::Queue& ComputeQueue;
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
        float Anisotropy = 0.3f;
        uint32_t ViewStepCount = 64u;
        uint32_t SunStepCount = 6u;
    };

    struct BakeConstants
    {
        uint32_t Resolution;
        uint32_t WorleyPointsPerCell;
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
    void Init(const CloudSystemCreateInfo& createInfo);

    void CreateOutputImages(uint32_t width, uint32_t height);
    void CreateNoiseTexture();
    void CreateDescriptorSetLayout();
    void CreateBakeDescriptorSetLayout();
    void CreatePipelineLayout(vk::raii::DescriptorSetLayout& globalSetLayout,
                              vk::raii::DescriptorSetLayout& depthSetLayout);
    void CreateBakePipelineLayout();
    void CreatePipeline();
    void CreateBakePipeline();
    void CreateDescriptorPool();
    void CreateBakeDescriptorPool();
    void AllocateDescriptorSets();
    void AllocateAndWriteBakeDescriptorSet();
    void WriteDescriptorSets();
    void BakeNoiseTexture(vk::raii::CommandPool& commandPool,
                          vk::raii::Queue& computeQueue);

private:
    static const uint32_t s_NOISE_RES;

    vk::raii::Device& m_Device;
    vk::raii::PhysicalDevice& m_PhysicalDevice;

    vk::raii::DescriptorSetLayout m_SetLayout = nullptr;
    vk::raii::DescriptorSetLayout m_BakeSetLayout = nullptr;
    vk::raii::DescriptorPool m_DescriptorPool = nullptr;
    vk::raii::DescriptorPool m_BakeDescriptorPool = nullptr;
    vk::raii::PipelineLayout m_PipelineLayout = nullptr;
    vk::raii::PipelineLayout m_BakePipelineLayout = nullptr;
    vk::raii::Pipeline m_Pipeline = nullptr;
    vk::raii::Pipeline m_BakePipeline = nullptr;
    std::vector<vk::raii::DescriptorSet> m_DescriptorSets;
    vk::raii::DescriptorSet m_BakeDescriptorSet = nullptr;

    std::vector<vk::raii::Image> m_OutputImages;
    std::vector<vk::raii::DeviceMemory> m_OutputImageMemory;
    std::vector<vk::raii::ImageView> m_OutputViews;
    vk::raii::Image m_PerlinWorleyImage = nullptr;
    vk::raii::DeviceMemory m_PerlinWorleyMemory = nullptr;
    vk::raii::ImageView m_PerlinWorleyView = nullptr;

    const uint32_t m_FramesInFlight;
    uint32_t m_Width;
    uint32_t m_Height;
    uint32_t m_OutputWidth;
    uint32_t m_OutputHeight;

    CloudPushConstants m_CloudData{};
};
