#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Hikari::Asset
{

// Synchronous CPU encoder invocation. Argument vectors never pass through a shell.
// Output goes to a transaction-local file so child pipe buffers cannot deadlock.
bool RunCookProcess(const std::filesystem::path& executable,
                    const std::vector<std::string>& arguments, const std::filesystem::path& log,
                    std::string& error, bool bCheckHpcFallback = true);
std::string CookPathUtf8(const std::filesystem::path& path);

} // namespace Hikari::Asset
