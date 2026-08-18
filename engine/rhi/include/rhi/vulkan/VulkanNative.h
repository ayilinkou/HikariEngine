#pragma once

#include <cstdint>

#include "vk_mem_alloc.h"
#include "vulkan/vulkan_raii.hpp"

#include <rhi/IDevice.h>

// The one sanctioned way to get a Vulkan handle out of an IDevice.
//
// It exists because ImGui's Vulkan backend needs raw instance, physical device,
// device, queue family index and queue handles, and wrapping ImGui to avoid that
// is not worth doing. Anything that reaches in here is by definition
// backend-specific and will not compile against a second backend — which is the
// point: the leak is *listed*, in one file, rather than spread through the
// renderer where nobody can count it.
//
// The RAII accessors below are a wider hole than the ImGui one, and a temporary
// one. They exist because the renderer still creates Vulkan objects directly —
// swapchains, pipelines, descriptor sets — and cannot do that from raw C
// handles. Every resource type that moves behind IDevice removes callers from
// this list, and the last of them removes the accessors.
namespace Rhi::Vulkan
{
// Raw handles, for C APIs such as ImGui that take them by value.
struct NativeDevice
{
    VkInstance Instance = VK_NULL_HANDLE;
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    VkQueue GraphicsQueue = VK_NULL_HANDLE;
    uint32_t GraphicsQueueFamily = ~0u;
    uint32_t ApiVersion = 0;
};

NativeDevice GetNative(IDevice& device);

// Transitional accessors for code that still builds Vulkan objects itself.
// Each returns a reference into the device, so it stays valid for as long as the
// device does and must not outlive it.
//
// There is deliberately no instance accessor here: nothing outside this module
// needs one now that surface creation lives inside it, and the raw handle is
// already in NativeDevice for ImGui's benefit.
vk::raii::PhysicalDevice& GetPhysicalDevice(IDevice& device);
vk::raii::Device& GetDevice(IDevice& device);
vk::raii::SurfaceKHR& GetSurface(IDevice& device);
vk::raii::Queue& GetGraphicsQueue(IDevice& device);
uint32_t GetGraphicsQueueFamily(IDevice& device);
VmaAllocator GetAllocator(IDevice& device);
} // namespace Rhi::Vulkan
