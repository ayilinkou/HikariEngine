#pragma once

#include <optional>
#include <string>

#include <asset/ImageReader.h>
#include <asset/TextureSettings.h>

namespace Hikari::Asset
{

/**
 * Area-weighted box reduction, including odd edges. Normal RGB is decoded to
 * signed XYZ, averaged, then normalized; alpha remains a numeric average.
 */
std::optional<Image> FilterTextureMip(const Image& source, TextureColorSpace color,
                                      std::string& error, TextureUsage usage = TextureUsage::Color);

/** Normalize decoded XYZ before storing XY with positive-Z reconstruction. */
std::optional<Image> PrepareNormalTextureXY(const Image& source, std::string& error);

} // namespace Hikari::Asset
