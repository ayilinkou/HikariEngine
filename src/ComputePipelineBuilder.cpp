#include "ComputePipelineBuilder.h"

#include "Utility.h"

ComputePipelineBuilder::ComputePipelineBuilder(vk::raii::Device& device)
    : m_Device(device)
{
}

ComputePipelineBuilder&
ComputePipelineBuilder::Shader(const std::string& spvPath, const char* entry)
{
    m_SpvPath = spvPath;
    m_Entry = entry;
    return *this;
}

ComputePipelineBuilder& ComputePipelineBuilder::Layout(
    std::span<const vk::DescriptorSetLayout> setLayouts,
    std::span<const vk::PushConstantRange> pushRanges)
{
    m_SetLayouts.assign(setLayouts.begin(), setLayouts.end());
    m_PushRanges.assign(pushRanges.begin(), pushRanges.end());
    return *this;
}

ComputePipelineBuilder& ComputePipelineBuilder::DebugName(std::string name)
{
    m_DebugName = std::move(name);
    return *this;
}

std::pair<vk::raii::PipelineLayout, vk::raii::Pipeline>
ComputePipelineBuilder::Build()
{
    if (m_SpvPath.empty())
        throw std::runtime_error("ComputePipelineBuilder: no shader path set!");

    // --- Shader module + stage ---
    auto code = ReadFile(m_SpvPath);
    vk::ShaderModuleCreateInfo moduleInfo{
        .codeSize = code.size(),
        .pCode = reinterpret_cast<const uint32_t*>(code.data())};
    vk::raii::ShaderModule shaderModule(m_Device, moduleInfo);

    vk::PipelineShaderStageCreateInfo stageInfo{
        .stage = vk::ShaderStageFlagBits::eCompute,
        .module = *shaderModule,
        .pName = m_Entry.c_str()};

    // --- Pipeline layout ---
    vk::PipelineLayoutCreateInfo layoutInfo{
        .setLayoutCount = static_cast<uint32_t>(m_SetLayouts.size()),
        .pSetLayouts = m_SetLayouts.data(),
        .pushConstantRangeCount = static_cast<uint32_t>(m_PushRanges.size()),
        .pPushConstantRanges = m_PushRanges.data()};
    vk::raii::PipelineLayout pipelineLayout(m_Device, layoutInfo);

    // --- Pipeline ---
    vk::ComputePipelineCreateInfo pipelineInfo{.stage = stageInfo,
                                               .layout = *pipelineLayout};
    vk::raii::Pipeline pipeline(m_Device, nullptr, pipelineInfo);

    if (!m_DebugName.empty())
    {
        SetVkDebugName(m_Device, *shaderModule, vk::ObjectType::eShaderModule,
                       (m_DebugName + " Compute Pipeline Shader Module").c_str());
        SetVkDebugName(m_Device, *pipeline, vk::ObjectType::ePipeline,
                       (m_DebugName + " Compute Pipeline").c_str());
        SetVkDebugName(m_Device, *pipelineLayout,
                       vk::ObjectType::ePipelineLayout,
                       (m_DebugName + " Compute Pipeline Layout").c_str());
    }

    return {std::move(pipelineLayout), std::move(pipeline)};
}
