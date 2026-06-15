#pragma once

#include "vulkan/vulkan_raii.hpp"

struct FrameData
{
    vk::raii::CommandBuffer GenericCommandBuffer = nullptr;
    vk::raii::CommandBuffer OpaqueCommandBuffer = nullptr;
    vk::raii::CommandBuffer TransparentCommandBuffer = nullptr;
    vk::raii::CommandBuffer ImGuiCommandBuffer = nullptr;
    vk::raii::CommandBuffer DrawLayoutCommandBuffer = nullptr;
    vk::raii::CommandBuffer PresentLayoutCommandBuffer = nullptr;
    vk::raii::Semaphore PresentCompleteSemaphore = nullptr;
    vk::raii::Fence DrawFence = nullptr;
    vk::raii::Buffer UniformBuffer = nullptr;
    vk::raii::DeviceMemory UniformBufferMemory = nullptr;
	vk::raii::Buffer InstanceBuffer = nullptr;
	vk::raii::DeviceMemory InstanceBufferMemory = nullptr;
    void* UniformBufferMapping = nullptr;
	void* InstanceBufferMapping = nullptr;
};
