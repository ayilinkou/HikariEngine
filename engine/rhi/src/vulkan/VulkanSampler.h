#pragma once

#include "vulkan/vulkan_raii.hpp"

namespace Rhi::Vulkan
{
// What a SamplerHandle resolves to. A wrapper for the same reason
// VulkanTextureView is one: HandlePool needs a default-constructible payload
// and vk::raii::Sampler has no default constructor.
struct VulkanSampler
{
    vk::raii::Sampler Sampler = nullptr;
};
} // namespace Rhi::Vulkan
