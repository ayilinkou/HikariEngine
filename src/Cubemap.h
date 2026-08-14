#pragma once

#include "AllocatedImage.h"

struct CubemapCreateInfo
{
    std::string RightPath = "";
    std::string LeftPath = "";
    std::string TopPath = "";
    std::string BottomPath = "";
    std::string FrontPath = "";
    std::string BackPath = "";
    std::string Name = "Cubemap";
    vk::Format Format = vk::Format::eUndefined;

    std::string Key() const
    {
        return RightPath + LeftPath + TopPath + BottomPath + FrontPath + BackPath;
    }
};

class Cubemap
{
public:
    Cubemap() = default;
    Cubemap(AllocatedImage image, vk::raii::ImageView imageView,
            const CubemapCreateInfo& createInfo);

    vk::Image GetImage() { return m_Image.Image; }
    vk::ImageView GetImageView() { return *m_ImageView; }

    const std::string& GetName() const { return m_CreateInfo.Name; }
    const CubemapCreateInfo& GetCreateInfo() const { return m_CreateInfo; };

private:
    AllocatedImage m_Image{};
    vk::raii::ImageView m_ImageView = nullptr;

    CubemapCreateInfo m_CreateInfo{};
};
