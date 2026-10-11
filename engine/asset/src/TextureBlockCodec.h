#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <asset/DdsTextureInfo.h>
#include <asset/ImageReader.h>

namespace Hikari::Asset
{

std::optional<std::vector<uint8_t>> ReadTextureBlocks(const std::filesystem::path& path,
                                                      const DdsTextureInfo& info, uint32_t level,
                                                      uint32_t face, std::string& error);
/** Decode one native subresource, retaining signed components via an UNORM working remap. */
std::optional<Image> DecodeTextureBlocks(std::span<const uint8_t> blocks, Core::Extent2D extent,
                                         TextureCompression format,
                                         const std::filesystem::path& directory,
                                         const std::filesystem::path& executable,
                                         std::string& error);
/** Internal native DDS encoding includes BC2/BC3 and signed BC4/BC5. */
std::optional<std::vector<uint8_t>>
EncodeTextureBlocks(const Image& image, TextureCompression format, uint32_t threads,
                    double alphaCutoff, const std::filesystem::path& directory,
                    const std::filesystem::path& executable, std::string& error);

} // namespace Hikari::Asset
