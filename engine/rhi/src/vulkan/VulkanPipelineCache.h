#pragma once

#include <filesystem>

#include "vulkan/vulkan_raii.hpp"

#include <rhi/PipelineCache.h>

namespace Hikari::Rhi::Vulkan
{
/**
 * IPipelineCache over a VkPipelineCache, seeded from and saved to a file.
 *
 * The blob on disk is whatever vkGetPipelineCacheData produced, prefixed by the
 * 32-byte header Vulkan defines for exactly this purpose: it carries the vendor
 * ID, device ID and pipeline cache UUID of the device that wrote it, so a file
 * left behind by another GPU or an updated driver can be recognised and dropped
 * instead of being handed back to a driver that cannot use it.
 */
class VulkanPipelineCache final : public IPipelineCache
{
public:
    VulkanPipelineCache(vk::raii::Device& device,
                        const vk::PhysicalDeviceProperties& deviceProperties,
                        const PipelineCacheDesc& desc);

    bool Save() override;

    const vk::raii::PipelineCache& Get() const { return m_Cache; }

private:
    vk::raii::PipelineCache m_Cache = nullptr;
    std::filesystem::path m_Path;
};

/**
 * The Vulkan cache behind a neutral one. Throws if it came from another
 * backend.
 */
VulkanPipelineCache& ToVulkan(IPipelineCache& cache);

/**
 * The same thing in the shape vulkan.hpp's RAII pipeline constructors take.
 *
 * Nullable because building a pipeline without a cache is legal — it is what a
 * builder that was never given one does, and what tests do — and vk::Optional's
 * null state is how those constructors spell it.
 */
vk::Optional<const vk::raii::PipelineCache> GetVkPipelineCache(IPipelineCache* pCache);
} // namespace Hikari::Rhi::Vulkan
