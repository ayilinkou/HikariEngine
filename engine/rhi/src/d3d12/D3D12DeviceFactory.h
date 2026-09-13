#pragma once

#include <memory>

#include <rhi/DeviceDesc.h>
#include <rhi/IDevice.h>

namespace Hikari::Rhi::D3D12
{

/**
 * Builds the D3D12 device. Declared apart from D3D12Device.h for the reason
 * VulkanDeviceFactory.h is: the neutral dispatcher in Backend.cpp names every
 * backend, and should include none of their API headers to do it.
 */
[[nodiscard]] std::unique_ptr<IDevice> CreateD3D12Device(const DeviceDesc& desc);

} // namespace Hikari::Rhi::D3D12
