#pragma once

#include <optional>
#include <string>

#include <asset/ImageReader.h>
#include <asset/TextureSettings.h>

namespace Hikari::Asset
{

// Area-weighted box reduction, preserving every source row/column at odd extents.
// sRGB RGB channels filter in linear light; alpha/data channels use numeric averages.
std::optional<Image> FilterTextureMip(const Image& source, TextureColorSpace color,
                                      std::string& error);

} // namespace Hikari::Asset
