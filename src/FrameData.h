#pragma once

#include "vulkan/vulkan_raii.hpp"

#include "Texture.h"

struct FrameData
{
    vk::raii::CommandPool DrawLayoutCommandPool = nullptr;
    vk::raii::CommandPool OpaqueCommandPool = nullptr;
    vk::raii::CommandPool TransparentCommandPool = nullptr;
    vk::raii::CommandPool CompositeCommandPool = nullptr;
    vk::raii::CommandPool ImGuiCommandPool = nullptr;
    vk::raii::CommandPool PresentLayoutCommandPool = nullptr;
    vk::raii::CommandBuffer DrawLayoutCommandBuffer = nullptr;
    vk::raii::CommandBuffer OpaqueCommandBuffer = nullptr;
    vk::raii::CommandBuffer TransparentCommandBuffer = nullptr;
    vk::raii::CommandBuffer CompositeCommandBuffer = nullptr;
    vk::raii::CommandBuffer ImGuiCommandBuffer = nullptr;
    vk::raii::CommandBuffer PresentLayoutCommandBuffer = nullptr;
    vk::raii::Semaphore PresentCompleteSemaphore = nullptr;
    vk::raii::Fence DrawFence = nullptr;
    vk::raii::DescriptorSet GlobalBufferDescriptorSet = nullptr;
    vk::raii::DescriptorSet CompositeDescriptorSet = nullptr;
    Texture OpaqueTexture;
    Texture AccumTexture;
    Texture RevealageTexture;
    vk::raii::Buffer GlobalBuffer = nullptr;
    vk::raii::DeviceMemory GlobalBufferMemory = nullptr;
    vk::raii::Buffer InstanceBuffer = nullptr;
    vk::raii::DeviceMemory InstanceBufferMemory = nullptr;
    void* GlobalBufferMapping = nullptr;
    void* InstanceBufferMapping = nullptr;
};
