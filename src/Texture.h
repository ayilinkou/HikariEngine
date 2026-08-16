#pragma once

#include <cstdint>
#include <string>

#include "vulkan/vulkan_raii.hpp"

#include "AllocatedImage.h"

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
    Texture() = default;
    Texture(AllocatedImage image, vk::raii::ImageView imageView, const std::string& path);

    vk::Image GetImage() { return m_Image.Image; }
    vk::ImageView GetImageView() { return *m_ImageView; }

    const std::string& GetPath() const { return m_Path; }

private:
    AllocatedImage m_Image;
    vk::raii::ImageView m_ImageView = nullptr;

    std::string m_Name = "Name";
    std::string m_Path = "";
};
