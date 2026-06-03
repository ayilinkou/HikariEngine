#pragma once

#include "vulkan/vulkan_raii.hpp"

struct FrameData
{
    vk::raii::CommandBuffer CommandBuffer = nullptr;
    vk::raii::Semaphore PresentCompleteSemaphore = nullptr;
    vk::raii::Fence DrawFence = nullptr;
    vk::raii::Buffer UniformBuffer = nullptr;
    vk::raii::DeviceMemory UniformBufferMemory = nullptr;
    void* UniformBufferMapping = nullptr;
};
