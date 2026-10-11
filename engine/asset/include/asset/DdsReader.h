#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <asset/DdsTextureInfo.h>

namespace Hikari::Asset
{

// Read bounded headers and enumerate tightly packed native BC payloads without
// allocating image data. Reject malformed, unsupported and ambiguous layouts.
std::optional<DdsTextureInfo> ReadDdsInfo(const std::filesystem::path& path, std::string& error);

} // namespace Hikari::Asset
