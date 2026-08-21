#include <rhi/vulkan/PipelineBuilder.h>

#include <platform/FileSystem.h>

#include <rhi/vulkan/DebugNames.h>

PipelineBuilder::PipelineBuilder(vk::raii::Device& device) : m_Device(device) {}

PipelineBuilder& PipelineBuilder::Shaders(const std::string& spvPath, const char* vertEntry,
                                          const char* fragEntry)
{
    m_SpvPath = spvPath;
    m_VertEntry = vertEntry;
    m_FragEntry = fragEntry;
    return *this;
}

PipelineBuilder&
PipelineBuilder::VertexInput(std::span<const vk::VertexInputBindingDescription> bindings,
                             std::span<const vk::VertexInputAttributeDescription> attributes)
{
    m_Bindings.assign(bindings.begin(), bindings.end());
    m_Attributes.assign(attributes.begin(), attributes.end());
    return *this;
}

PipelineBuilder& PipelineBuilder::Depth(bool test, bool write, vk::CompareOp op)
{
    m_bDepthTest = test;
    m_bDepthWrite = write;
    m_DepthCompareOp = op;
    return *this;
}

PipelineBuilder& PipelineBuilder::ColorAttachments(
    std::span<const vk::Format> formats,
    std::span<const vk::PipelineColorBlendAttachmentState> blendStates)
{
    m_ColorFormats.assign(formats.begin(), formats.end());
    m_BlendStates.assign(blendStates.begin(), blendStates.end());
    return *this;
}

PipelineBuilder& PipelineBuilder::DepthAttachment(vk::Format format)
{
    m_DepthFormat = format;
    return *this;
}

PipelineBuilder& PipelineBuilder::Cull(vk::CullModeFlags mode, bool bDynamic)
{
    m_CullMode = mode;
    m_bDynamicCull = bDynamic;
    return *this;
}

PipelineBuilder& PipelineBuilder::Layout(std::span<const vk::DescriptorSetLayout> setLayouts,
                                         std::span<const vk::PushConstantRange> pushRanges)
{
    m_SetLayouts.assign(setLayouts.begin(), setLayouts.end());
    m_PushRanges.assign(pushRanges.begin(), pushRanges.end());
    return *this;
}

PipelineBuilder& PipelineBuilder::DebugName(std::string name)
{
    m_DebugName = std::move(name);
    return *this;
}

std::pair<vk::raii::PipelineLayout, vk::raii::Pipeline> PipelineBuilder::Build()
{
    if (m_SpvPath.empty())
        throw std::runtime_error("PipelineBuilder: no shader path set!");

    // --- Shader module + stages ---
    auto code = ReadFile(m_SpvPath);
    vk::ShaderModuleCreateInfo moduleInfo{.codeSize = code.size(),
                                          .pCode = reinterpret_cast<const uint32_t*>(code.data())};
    vk::raii::ShaderModule shaderModule(m_Device, moduleInfo);

    std::array<vk::PipelineShaderStageCreateInfo, 2> stages{
        vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eVertex,
                                          .module = *shaderModule,
                                          .pName = m_VertEntry.c_str()},
        vk::PipelineShaderStageCreateInfo{.stage = vk::ShaderStageFlagBits::eFragment,
                                          .module = *shaderModule,
                                          .pName = m_FragEntry.c_str()}};

    // --- Vertex input ---
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
        .vertexBindingDescriptionCount = static_cast<uint32_t>(m_Bindings.size()),
        .pVertexBindingDescriptions = m_Bindings.data(),
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(m_Attributes.size()),
        .pVertexAttributeDescriptions = m_Attributes.data()};

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
        .topology = vk::PrimitiveTopology::eTriangleList, .primitiveRestartEnable = vk::False};

    // --- Viewport/scissor: always dynamic ---
    vk::PipelineViewportStateCreateInfo viewportState{.viewportCount = 1, .scissorCount = 1};

    // --- Rasterization ---
    vk::PipelineRasterizationStateCreateInfo rasterizer{.depthClampEnable = vk::False,
                                                        .rasterizerDiscardEnable = vk::False,
                                                        .polygonMode = vk::PolygonMode::eFill,
                                                        .cullMode = m_CullMode,
                                                        .frontFace =
                                                            vk::FrontFace::eCounterClockwise,
                                                        .depthBiasEnable = vk::False,
                                                        .lineWidth = 1.f};

    // --- Multisampling (no MSAA for now) ---
    vk::PipelineMultisampleStateCreateInfo multisampling{
        .rasterizationSamples = vk::SampleCountFlagBits::e1, .sampleShadingEnable = vk::False};

    // --- Depth/stencil ---
    vk::PipelineDepthStencilStateCreateInfo depthStencil{.depthTestEnable = m_bDepthTest,
                                                         .depthWriteEnable = m_bDepthWrite,
                                                         .depthCompareOp = m_DepthCompareOp,
                                                         .depthBoundsTestEnable = vk::False,
                                                         .stencilTestEnable = vk::False};

    // --- Color blend ---
    vk::PipelineColorBlendStateCreateInfo colorBlending{
        .logicOpEnable = vk::False,
        .attachmentCount = static_cast<uint32_t>(m_BlendStates.size()),
        .pAttachments = m_BlendStates.data()};

    // --- Dynamic state ---
    std::vector<vk::DynamicState> dynamicStates = {vk::DynamicState::eViewport,
                                                   vk::DynamicState::eScissor};
    if (m_bDynamicCull)
        dynamicStates.push_back(vk::DynamicState::eCullMode);

    vk::PipelineDynamicStateCreateInfo dynamicState{.dynamicStateCount =
                                                        static_cast<uint32_t>(dynamicStates.size()),
                                                    .pDynamicStates = dynamicStates.data()};

    // --- Pipeline layout ---
    vk::PipelineLayoutCreateInfo layoutInfo{
        .setLayoutCount = static_cast<uint32_t>(m_SetLayouts.size()),
        .pSetLayouts = m_SetLayouts.data(),
        .pushConstantRangeCount = static_cast<uint32_t>(m_PushRanges.size()),
        .pPushConstantRanges = m_PushRanges.data()};
    vk::raii::PipelineLayout pipelineLayout(m_Device, layoutInfo);

    // --- Dynamic rendering info ---
    vk::PipelineRenderingCreateInfo renderingInfo{.colorAttachmentCount =
                                                      static_cast<uint32_t>(m_ColorFormats.size()),
                                                  .pColorAttachmentFormats = m_ColorFormats.data(),
                                                  .depthAttachmentFormat = m_DepthFormat};

    vk::GraphicsPipelineCreateInfo pipelineInfo{.pNext = &renderingInfo,
                                                .stageCount = static_cast<uint32_t>(stages.size()),
                                                .pStages = stages.data(),
                                                .pVertexInputState = &vertexInputInfo,
                                                .pInputAssemblyState = &inputAssembly,
                                                .pViewportState = &viewportState,
                                                .pRasterizationState = &rasterizer,
                                                .pMultisampleState = &multisampling,
                                                .pDepthStencilState = &depthStencil,
                                                .pColorBlendState = &colorBlending,
                                                .pDynamicState = &dynamicState,
                                                .layout = *pipelineLayout,
                                                .renderPass = nullptr, // dynamic rendering
                                                .subpass = 0};

    vk::raii::Pipeline pipeline(m_Device, nullptr, pipelineInfo);

    if (!m_DebugName.empty())
    {
        SetVkDebugName(m_Device, *shaderModule, vk::ObjectType::eShaderModule,
                       (m_DebugName + " Pipeline Shader Module").c_str());
        SetVkDebugName(m_Device, *pipeline, vk::ObjectType::ePipeline,
                       (m_DebugName + " Pipeline").c_str());
        SetVkDebugName(m_Device, *pipelineLayout, vk::ObjectType::ePipelineLayout,
                       (m_DebugName + " Pipeline Layout").c_str());
    }

    return {std::move(pipelineLayout), std::move(pipeline)};
}
