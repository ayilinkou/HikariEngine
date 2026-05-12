#pragma once

#include "vulkan/vulkan_raii.hpp"

struct FrameData
{
    vk::raii::CommandBuffer m_CommandBuffer = nullptr;
    vk::raii::Semaphore m_PresentCompleteSemaphore = nullptr;
    vk::raii::Fence m_DrawFence = nullptr;
    vk::raii::Buffer m_UniformBuffer = nullptr;
    vk::raii::DeviceMemory m_UniformBufferMemory = nullptr;
    void* m_UniformBufferMapping = nullptr;
};
