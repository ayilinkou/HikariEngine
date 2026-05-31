#include "Texture.h"

Texture::Texture(vk::raii::Image image, vk::raii::ImageView imageView,
                 vk::raii::DeviceMemory deviceMemory, const std::string& path)
    : m_Image(std::move(image)), m_ImageMemory(std::move(deviceMemory)),
      m_ImageView(std::move(imageView)), m_Name(path), m_Path(path)
{
}
