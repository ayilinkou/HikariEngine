#pragma once

#include "vulkan/vulkan_raii.hpp"

enum TextureBinding : uint8_t
{
    Albedo,
    Normal,
    MetallicRoughness,

    COUNT
};

class Texture
{
public:
    Texture(vk::raii::Image image, vk::raii::ImageView imageView,
            vk::raii::DeviceMemory deviceMemory, const std::string& path);

    vk::raii::Image& GetImage() { return m_Image; }
    vk::raii::DeviceMemory& GetImageMemory() { return m_ImageMemory; }
    vk::raii::ImageView& GetImageView() { return m_ImageView; }
	
	const std::string& GetPath() { return m_Path; }

private:
    vk::raii::Image m_Image = nullptr;
    vk::raii::DeviceMemory m_ImageMemory = nullptr;
    vk::raii::ImageView m_ImageView = nullptr;

    std::string m_Name = "Name";
    std::string m_Path = "";
};
