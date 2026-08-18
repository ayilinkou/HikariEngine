#pragma once

#include <span>
#include <string>
#include <utility>
#include <vector>

#include "vulkan/vulkan_raii.hpp"

class ComputePipelineBuilder
{
public:
    explicit ComputePipelineBuilder(vk::raii::Device& device);

    ComputePipelineBuilder& Shader(const std::string& spvPath, const char* entry = "main");
    ComputePipelineBuilder& Layout(std::span<const vk::DescriptorSetLayout> setLayouts,
                                   std::span<const vk::PushConstantRange> pushRanges);
    ComputePipelineBuilder& DebugName(std::string name);

    [[nodiscard]] std::pair<vk::raii::PipelineLayout, vk::raii::Pipeline> Build();

private:
    vk::raii::Device& m_Device;
    std::string m_SpvPath;
    std::string m_Entry = "main";
    std::vector<vk::DescriptorSetLayout> m_SetLayouts;
    std::vector<vk::PushConstantRange> m_PushRanges;
    std::string m_DebugName;
};
