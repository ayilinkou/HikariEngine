#include <rhi/vulkan/VulkanNative.h>

#include <stdexcept>

#include "vulkan/VulkanDevice.h"

namespace Rhi::Vulkan
{
namespace
{
// A dynamic_cast rather than a static_cast because getting this wrong is
// undefined behaviour rather than a diagnosable error, and the cost is
// irrelevant: these are called a handful of times during startup, never per
// frame. If a second backend ever exists, this is where passing the wrong
// device type surfaces as an exception instead of as memory corruption.
VulkanDevice& AsVulkan(IDevice& device)
{
    auto* pVulkanDevice = dynamic_cast<VulkanDevice*>(&device);
    if (!pVulkanDevice)
        throw std::runtime_error("Rhi::Vulkan native accessor used on a non-Vulkan device!");
    return *pVulkanDevice;
}
} // namespace

NativeDevice GetNative(IDevice& device)
{
    VulkanDevice& vulkanDevice = AsVulkan(device);
    return NativeDevice{.Instance = *vulkanDevice.GetInstance(),
                        .PhysicalDevice = *vulkanDevice.GetPhysicalDevice(),
                        .Device = *vulkanDevice.GetDevice(),
                        .GraphicsQueue = *vulkanDevice.GetGraphicsQueue(),
                        .GraphicsQueueFamily = vulkanDevice.GetQueueFamily(QueueType::Graphics),
                        .ApiVersion = vulkanDevice.GetApiVersion()};
}

vk::raii::PhysicalDevice& GetPhysicalDevice(IDevice& device)
{
    return AsVulkan(device).GetPhysicalDevice();
}

vk::raii::Device& GetDevice(IDevice& device)
{
    return AsVulkan(device).GetDevice();
}

vk::raii::SurfaceKHR& GetSurface(IDevice& device)
{
    return AsVulkan(device).GetSurface();
}

vk::raii::Queue& GetGraphicsQueue(IDevice& device)
{
    return AsVulkan(device).GetGraphicsQueue();
}

uint32_t GetGraphicsQueueFamily(IDevice& device)
{
    return AsVulkan(device).GetQueueFamily(QueueType::Graphics);
}

VmaAllocator GetAllocator(IDevice& device)
{
    return AsVulkan(device).GetAllocator();
}

vk::Buffer GetBuffer(IDevice& device, BufferHandle handle)
{
    return AsVulkan(device).GetBuffer(handle);
}
} // namespace Rhi::Vulkan
