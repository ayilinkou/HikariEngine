#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <asset/DdsTextureInfo.h>
#include <asset/ImageReader.h>

namespace Hikari::Asset
{

// Read bounded headers and enumerate tightly packed native BC payloads without
// allocating image data. Reject malformed, unsupported and ambiguous layouts.
std::optional<DdsTextureInfo> ReadDdsInfo(const std::filesystem::path& path, std::string& error);
std::optional<Image> ReadDdsPixels(const std::filesystem::path& path, const DdsTextureInfo& info,
                                   uint32_t level, uint32_t face, std::string& error);

} // namespace Hikari::Asset
