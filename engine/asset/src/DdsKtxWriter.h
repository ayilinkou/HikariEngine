#pragma once

#include <filesystem>
#include <string>

#include <asset/DdsTextureInfo.h>
#include <asset/ResolvedTextureSettings.h>

namespace Hikari::Asset
{

// Repackage supplied levels only. No decoder, filtering or compressor is used.
// The caller handles missing-mip completion before reporting a full-chain cook.
bool RepackageDds(const std::filesystem::path& source, const DdsTextureInfo& info,
                  const ResolvedTextureSettings& settings, const std::filesystem::path& output,
                  std::string& error);

} // namespace Hikari::Asset
