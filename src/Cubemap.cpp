#include "Cubemap.h"

Cubemap::Cubemap(AllocatedImage image, vk::raii::ImageView imageView,
                 const CubemapCreateInfo& createInfo)
    : m_Image(std::move(image)), m_ImageView(std::move(imageView)),
      m_CreateInfo(createInfo)
{
}
