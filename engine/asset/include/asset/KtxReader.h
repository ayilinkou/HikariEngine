#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <asset/TextureData.h>

namespace Hikari::Asset
{
/**
 * Load native BC KTX2 2D textures and single cubemaps without source files or
 * cook records. Every supplied mip/face is retained. No transcoding, implicit
 * mip generation, orientation changes or graphics API upload is performed.
 * Returns nothing on malformed/unsupported input, with a path-bearing error.
 */
[[nodiscard]] std::optional<TextureData> ReadKtxTexture(const std::filesystem::path& path,
                                                        std::string& error);
} // namespace Hikari::Asset
