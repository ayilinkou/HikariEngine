#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace Hikari::Asset
{

enum class TextureCookSourceKind
{
    Image,
    Dds,
    CubemapDescriptor
};

struct TextureCookPaths
{
    std::filesystem::path SourceRoot;
    std::filesystem::path CookedRoot;
    std::filesystem::path SourcePath;
    std::filesystem::path SourceRelative;
    std::filesystem::path SettingsPath;
    std::filesystem::path CookedPath;
    std::filesystem::path RecordPath;
    TextureCookSourceKind Kind = TextureCookSourceKind::Image;
};

/**
 * Map a source-relative or absolute existing input into source-adjacent settings
 * and separate generated paths. Resolve symlinks before enforcing root bounds.
 * An empty cooked root selects the resolved source root's sibling cooked/.
 */
std::optional<TextureCookPaths> MapTextureCookPaths(const std::filesystem::path& sourceRoot,
                                                    const std::filesystem::path& cookedRoot,
                                                    const std::filesystem::path& source,
                                                    std::string& error);

} // namespace Hikari::Asset
