#pragma once

#include "vulkan/vulkan_raii.hpp"

namespace Hikari::Rhi::Vulkan
{
/**
 * What a TextureViewHandle resolves to.
 *
 * A struct wrapping one vk::raii::ImageView rather than the raii type itself,
 * because Core::HandlePool requires a default-constructible payload and
 * vk::raii::ImageView has no default constructor — only one taking nullptr.
 * The wrapper is what supplies it, and keeping the raii type is what makes
 * releasing a pool slot destroy the view.
 */
struct VulkanTextureView
{
    vk::raii::ImageView View = nullptr;
};
} // namespace Hikari::Rhi::Vulkan
