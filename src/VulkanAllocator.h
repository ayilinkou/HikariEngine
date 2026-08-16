#pragma once

#include <cstdint>
#include <stdexcept>
#include <utility>

#include "vk_mem_alloc.h"
#include "vulkan/vulkan_raii.hpp"

class VulkanAllocator
{
public:
    VulkanAllocator() = default;
    VulkanAllocator(vk::raii::Instance& instance, vk::raii::PhysicalDevice& physicalDevice,
                    vk::raii::Device& device, uint32_t vulkanApiVersion)
    {
        VmaAllocatorCreateInfo allocatorInfo{};
        allocatorInfo.physicalDevice = *physicalDevice;
        allocatorInfo.device = *device;
        allocatorInfo.instance = *instance;
        allocatorInfo.vulkanApiVersion = vulkanApiVersion;

        vk::Result result =
            static_cast<vk::Result>(vmaCreateAllocator(&allocatorInfo, &m_Allocator));

        if (result != vk::Result::eSuccess)
            throw std::runtime_error("Failed to create VMA allocator!");
    }

    VulkanAllocator(const VulkanAllocator&) = delete;
    VulkanAllocator& operator=(const VulkanAllocator&) = delete;

    VulkanAllocator(VulkanAllocator&& other) noexcept { *this = std::move(other); }

    VulkanAllocator& operator=(VulkanAllocator&& other) noexcept
    {
        if (this != &other)
        {
            Destroy();
            m_Allocator = other.m_Allocator;
            other.m_Allocator = nullptr;
        }
        return *this;
    }

    ~VulkanAllocator() { Destroy(); }

    // Implicit conversion so it drops straight into VMA C API calls,
    // e.g. vmaCreateBuffer(allocator, ...) where allocator is a VulkanAllocator&
    operator VmaAllocator() const { return m_Allocator; }

private:
    void Destroy()
    {
        if (m_Allocator)
            vmaDestroyAllocator(m_Allocator);
    }

    VmaAllocator m_Allocator{};
};
