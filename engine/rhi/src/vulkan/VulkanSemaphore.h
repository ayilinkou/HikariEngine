#pragma once

#include "vulkan/vulkan_raii.hpp"

#include <core/Handle.h>

namespace Hikari::Rhi::Vulkan
{
/**
 * A binary semaphore the present path owns. Private to the backend: a submission
 * names the image it writes, and the target finds the semaphores that order the
 * write, so no caller ever holds one.
 */
using SemaphoreHandle = Core::Handle<struct SemaphoreTag>;

/**
 * What a SemaphoreHandle resolves to. A wrapper for the same reason
 * VulkanSampler is one: Core::HandlePool needs a default-constructible payload and
 * vk::raii::Semaphore has no default constructor.
 *
 * Binary, not timeline. The only producer is the present path, and both
 * acquiring a swapchain image and presenting one are defined in terms of a
 * single-shot semaphore the caller never resets.
 */
struct VulkanSemaphore
{
    vk::raii::Semaphore Semaphore = nullptr;
};
} // namespace Hikari::Rhi::Vulkan
