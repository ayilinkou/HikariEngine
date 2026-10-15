#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <asset/TextureData.h>

namespace Hikari::Asset
{
/**
 * Synchronous acquisition, using the shared Release cooker for source requests
 * and libktx directly for cooked requests. Caches belong to the registry; a
 * first acquisition checks freshness, while a live cached asset needs no I/O.
 */
class TextureSource
{
public:
    TextureSource(std::filesystem::path sourceRoot, std::filesystem::path cookedRoot,
                  std::filesystem::path cookerExecutable, double encodingQuality = 1.0);

    // KTX2 paths are cooked-relative; all other paths are source-relative.
    // Resolve aliases before cache lookup, without requiring a cached file to
    // still exist. Absolute paths must stay inside the appropriate root.
    std::filesystem::path ResolvePath(const std::filesystem::path& requested) const;
    std::filesystem::path SettingsPath(const std::filesystem::path& resolved) const;
    std::optional<TextureData> Read(const std::filesystem::path& requested, std::string& error,
                                    std::vector<std::string>& warnings) const;

    const std::filesystem::path& CookedRoot() const { return m_CookedRoot; }

private:
    std::filesystem::path m_SourceRoot;
    std::filesystem::path m_CookedRoot;
    std::filesystem::path m_CookerExecutable;
    double m_EncodingQuality;
};
} // namespace Hikari::Asset
