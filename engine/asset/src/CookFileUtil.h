#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Hikari::Asset
{

bool ResolveCookRoots(const std::filesystem::path& source, const std::filesystem::path& cooked,
                      std::filesystem::path& resolvedSource, std::filesystem::path& resolvedCooked,
                      std::string& error);
// Root membership also handles native filesystem case rules for existing paths.
std::optional<std::filesystem::path> RelativeCookPath(const std::filesystem::path& path,
                                                      const std::filesystem::path& root);
std::optional<std::string> ReadCookText(const std::filesystem::path& path, std::string& error);
bool WriteCookText(const std::filesystem::path& path, std::string_view text, std::string& error);
bool CheckCookOutputDirectory(const std::filesystem::path& directory, std::string& error);
// Flush file bytes before publication, then atomically replace a file on the
// same filesystem. Windows needs explicit replacement semantics.
bool FlushCookFile(const std::filesystem::path& path, std::string& error);
bool ReplaceCookFile(const std::filesystem::path& temporary,
                     const std::filesystem::path& destination, std::string& error);

} // namespace Hikari::Asset
