#pragma once

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
};

class Cubemap
{
public:
    Cubemap() = default;
    Cubemap(vk::raii::Image image, vk::raii::ImageView imageView,
            vk::raii::DeviceMemory deviceMemory,
            const CubemapCreateInfo& createInfo);

    vk::raii::Image& GetImage() { return m_Image; }
    vk::raii::DeviceMemory& GetImageMemory() { return m_ImageMemory; }
    vk::raii::ImageView& GetImageView() { return m_ImageView; }

    const std::string& GetName() const { return m_CreateInfo.Name; }
	const CubemapCreateInfo& GetCreateInfo() const { return m_CreateInfo; };
private:
    vk::raii::Image m_Image = nullptr;
    vk::raii::DeviceMemory m_ImageMemory = nullptr;
    vk::raii::ImageView m_ImageView = nullptr;

    CubemapCreateInfo m_CreateInfo{};
};
