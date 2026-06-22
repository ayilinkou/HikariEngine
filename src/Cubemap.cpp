#include "Cubemap.h"

Cubemap::Cubemap(vk::raii::Image image, vk::raii::ImageView imageView,
        vk::raii::DeviceMemory deviceMemory,
        const CubemapCreateInfo& createInfo)
    : m_Image(std::move(image)), m_ImageMemory(std::move(deviceMemory)),
      m_ImageView(std::move(imageView)), m_CreateInfo(createInfo)
{
}
