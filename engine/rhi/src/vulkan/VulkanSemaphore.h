#pragma once

#include "vulkan/vulkan_raii.hpp"

namespace Rhi::Vulkan
{
// What a SemaphoreHandle resolves to. A wrapper for the same reason
// VulkanSampler is one: HandlePool needs a default-constructible payload and
// vk::raii::Semaphore has no default constructor.
//
// Binary, not timeline. The only producer is the present path, and both
// acquiring a swapchain image and presenting one are defined in terms of a
// single-shot semaphore the caller never resets.
struct VulkanSemaphore
{
    vk::raii::Semaphore Semaphore = nullptr;
};
} // namespace Rhi::Vulkan
