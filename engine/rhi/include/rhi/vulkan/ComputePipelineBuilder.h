#pragma once

#include <span>
#include <string>
#include <utility>
#include <vector>

#include "vulkan/vulkan_raii.hpp"

#include <rhi/PipelineCache.h>

class ComputePipelineBuilder
{
public:
    explicit ComputePipelineBuilder(vk::raii::Device& device);

    ComputePipelineBuilder& Shader(const std::string& spvPath, const char* entry = "main");
    ComputePipelineBuilder& Layout(std::span<const vk::DescriptorSetLayout> setLayouts,
                                   std::span<const vk::PushConstantRange> pushRanges);
    ComputePipelineBuilder& DebugName(std::string name);

    // Reuses whatever the driver has already compiled, and records what it
    // compiles here. Omitting it is legal and only costs compile time.
    ComputePipelineBuilder& Cache(Rhi::IPipelineCache& cache);

    [[nodiscard]] std::pair<vk::raii::PipelineLayout, vk::raii::Pipeline> Build();

private:
    vk::raii::Device& m_Device;
    std::string m_SpvPath;
    std::string m_Entry = "main";
    std::vector<vk::DescriptorSetLayout> m_SetLayouts;
    std::vector<vk::PushConstantRange> m_PushRanges;
    std::string m_DebugName;

    Rhi::IPipelineCache* m_pCache = nullptr;
};
