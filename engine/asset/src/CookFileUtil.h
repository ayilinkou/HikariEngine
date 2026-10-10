#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Hikari::Asset
{

// Root membership also handles native filesystem case rules for existing paths.
std::optional<std::filesystem::path> RelativeCookPath(const std::filesystem::path& path,
                                                      const std::filesystem::path& root);
std::optional<std::string> ReadCookText(const std::filesystem::path& path, std::string& error);
bool WriteCookText(const std::filesystem::path& path, std::string_view text, std::string& error);
bool CheckCookOutputDirectory(const std::filesystem::path& directory, std::string& error);

} // namespace Hikari::Asset
