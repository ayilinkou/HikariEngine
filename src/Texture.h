#pragma once

#include "vulkan/vulkan_raii.hpp"

class Texture
{
public:
    void LoadTexture(vk::raii::Device& device,
                     vk::raii::PhysicalDevice& physicalDevice,
                     vk::raii::CommandPool& commandPool,
                     vk::raii::Queue& transferQueue, const std::string& path,
                     const vk::Format format);

    vk::raii::Image& GetImage() { return m_Image; }
    vk::raii::DeviceMemory& GetImageMemory() { return m_ImageMemory; }
    vk::raii::ImageView& GetImageView() { return m_ImageView; }

private:
    void CreateTextureImage(vk::raii::Device& device,
                            vk::raii::PhysicalDevice& physicalDevice,
                            vk::raii::CommandPool& commandPool,
                            vk::raii::Queue& transferQueue,
                            const std::string& path, const vk::Format format);

private:
    vk::raii::Image m_Image = nullptr;
    vk::raii::DeviceMemory m_ImageMemory = nullptr;
    vk::raii::ImageView m_ImageView = nullptr;

    std::string m_Name = "Name";
};
