#pragma once

#include <memory>

#include <rhi/DeviceDesc.h>
#include <rhi/IDevice.h>

namespace Hikari::Rhi::Vulkan
{

/**
 * Builds the Vulkan device. Declared apart from VulkanDevice.h so that the
 * neutral dispatcher in Backend.cpp can reach it without pulling vulkan_raii
 * into its translation unit — the dispatcher is the one file in the module that
 * has to name every backend, and it should name none of their headers.
 */
[[nodiscard]] std::unique_ptr<IDevice> CreateVulkanDevice(const DeviceDesc& desc);

} // namespace Hikari::Rhi::Vulkan
