#pragma once

#include <span>
#include <string>
#include <utility>
#include <vector>

#include "vulkan/vulkan_raii.hpp"

#include <rhi/PipelineCache.h>

namespace Hikari::Rhi::Vulkan
{

class PipelineBuilder
{
public:
    explicit PipelineBuilder(vk::raii::Device& device);

    PipelineBuilder& Shaders(const std::string& spvPath, const char* vertEntry = "vertMain",
                             const char* fragEntry = "fragMain");
    PipelineBuilder& VertexInput(std::span<const vk::VertexInputBindingDescription> bindings,
                                 std::span<const vk::VertexInputAttributeDescription> attributes);
    PipelineBuilder& Depth(bool bTest, bool bWrite, vk::CompareOp op = vk::CompareOp::eLess);
    PipelineBuilder&
    ColorAttachments(std::span<const vk::Format> formats,
                     std::span<const vk::PipelineColorBlendAttachmentState> blendStates);
    PipelineBuilder& DepthAttachment(vk::Format format);
    PipelineBuilder& Cull(vk::CullModeFlags mode, bool bDynamic = false);
    PipelineBuilder& Layout(std::span<const vk::DescriptorSetLayout> setLayouts,
                            std::span<const vk::PushConstantRange> pushRanges);
    PipelineBuilder& DebugName(std::string name);

    /**
     * Reuses whatever the driver has already compiled, and records what it
     * compiles here. Omitting it is legal and only costs compile time.
     */
    PipelineBuilder& Cache(Rhi::IPipelineCache& cache);

    [[nodiscard]] std::pair<vk::raii::PipelineLayout, vk::raii::Pipeline> Build();

private:
    vk::raii::Device& m_Device;

    /** Shaders */
    std::string m_SpvPath;
    std::string m_VertEntry = "vertMain";
    std::string m_FragEntry = "fragMain";

    /** Vertex input */
    std::vector<vk::VertexInputBindingDescription> m_Bindings;
    std::vector<vk::VertexInputAttributeDescription> m_Attributes;

    /** Depth */
    bool m_bDepthTest = false;
    bool m_bDepthWrite = false;
    vk::CompareOp m_DepthCompareOp = vk::CompareOp::eLess;

    /** Attachments (dynamic rendering) */
    std::vector<vk::Format> m_ColorFormats;
    std::vector<vk::PipelineColorBlendAttachmentState> m_BlendStates;
    vk::Format m_DepthFormat = vk::Format::eUndefined;

    /** Rasterization */
    vk::CullModeFlags m_CullMode = vk::CullModeFlagBits::eNone;
    bool m_bDynamicCull = false;

    /** Layout */
    std::vector<vk::DescriptorSetLayout> m_SetLayouts;
    std::vector<vk::PushConstantRange> m_PushRanges;

    std::string m_DebugName;

    Rhi::IPipelineCache* m_pCache = nullptr;
};
} // namespace Hikari::Rhi::Vulkan
