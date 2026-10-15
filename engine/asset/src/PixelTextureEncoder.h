#pragma once

#include <filesystem>
#include <string>

#include <asset/PreparedTextureCook.h>

namespace Hikari::Asset
{

std::filesystem::path ConfiguredTextureEncoder();
bool EncodeTexturePixels(const PreparedTextureCook& prepared, const std::filesystem::path& output,
                         std::string& error,
                         const std::filesystem::path& executable = ConfiguredTextureEncoder());

} // namespace Hikari::Asset
