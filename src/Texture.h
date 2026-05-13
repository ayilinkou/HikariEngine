#pragma once

#include "vulkan/vulkan_raii.hpp"

class Texture
{
public:
    vk::raii::Image& GetImage() { return m_Image; }
    vk::raii::DeviceMemory& GetImageMemory() { return m_ImageMemory; }
    vk::raii::ImageView& GetImageView() { return m_ImageView; }

private:
    vk::raii::Image m_Image = nullptr;
    vk::raii::DeviceMemory m_ImageMemory = nullptr;
    vk::raii::ImageView m_ImageView = nullptr;
};
