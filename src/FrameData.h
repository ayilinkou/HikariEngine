#pragma once

#include "vulkan/vulkan_raii.hpp"

struct FrameData
{
    vk::raii::CommandBuffer CommandBuffer = nullptr;
    vk::raii::Semaphore PresentCompleteSemaphore = nullptr;
    vk::raii::Semaphore OpaqueCompleteSemaphore = nullptr;
    vk::raii::Semaphore TransparentCompleteSemaphore = nullptr;
    vk::raii::Semaphore ImGuiCompleteSemaphore = nullptr;
    vk::raii::Fence DrawFence = nullptr;
    vk::raii::Buffer UniformBuffer = nullptr;
    vk::raii::DeviceMemory UniformBufferMemory = nullptr;
	vk::raii::Buffer InstanceBuffer = nullptr;
	vk::raii::DeviceMemory InstanceBufferMemory = nullptr;
    void* UniformBufferMapping = nullptr;
	void* InstanceBufferMapping = nullptr;
};
