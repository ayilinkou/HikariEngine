#include "Texture.h"

Texture::Texture(AllocatedImage image, vk::raii::ImageView imageView,
                 const std::string& path)
    : m_Image(std::move(image)), m_ImageView(std::move(imageView)),
      m_Name(path), m_Path(path)
{
}
