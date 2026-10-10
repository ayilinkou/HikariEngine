#include <asset/TextureCookPaths.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <string_view>
#include <system_error>

#include "CookFileUtil.h"

namespace Hikari::Asset
{

std::optional<TextureCookPaths> MapTextureCookPaths(const std::filesystem::path& sourceRoot,
                                                    const std::filesystem::path& cookedRoot,
                                                    const std::filesystem::path& source,
                                                    std::string& error)
{
    namespace fs = std::filesystem;
    error.clear();
    std::error_code ec;
    if (sourceRoot.empty() || !fs::is_directory(sourceRoot, ec))
    {
        error = std::format("{}: Source root must be an existing directory; pass --source-root",
                            sourceRoot.string());
        return std::nullopt;
    }
    TextureCookPaths paths;
    paths.SourceRoot = fs::canonical(sourceRoot, ec);
    if (ec)
    {
        error =
            std::format("{}: Cannot resolve source root: {}", sourceRoot.string(), ec.message());
        return std::nullopt;
    }
    paths.CookedRoot =
        fs::weakly_canonical(cookedRoot.empty() ? paths.SourceRoot.parent_path() / "cooked"
                                                : fs::absolute(cookedRoot, ec),
                             ec);
    if (ec || paths.CookedRoot.empty())
    {
        error =
            std::format("{}: Cannot resolve cooked root: {}", cookedRoot.string(), ec.message());
        return std::nullopt;
    }
    if (fs::exists(paths.CookedRoot, ec) && !fs::is_directory(paths.CookedRoot, ec))
    {
        error = std::format("{}: Cooked root is not a directory; choose another --cooked-root",
                            paths.CookedRoot.string());
        return std::nullopt;
    }
    if (ec)
    {
        error = std::format("{}: Cannot inspect cooked root: {}. Choose another --cooked-root.",
                            paths.CookedRoot.string(), ec.message());
        return std::nullopt;
    }
    if (source.empty() || (source.has_root_path() && !source.is_absolute()))
    {
        error = "A source-relative or absolute texture path is required";
        return std::nullopt;
    }
    const auto requestedSource = source.is_absolute() ? source : paths.SourceRoot / source;
    paths.SourcePath = fs::canonical(requestedSource, ec);
    if (ec)
    {
        if (ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory)
            error = std::format("Source file was not found: {}", requestedSource.string());
        else
            error = std::format("Could not open source file {}: {}", requestedSource.string(),
                                ec.message());
        return std::nullopt;
    }
    if (!fs::is_regular_file(paths.SourcePath, ec))
    {
        error =
            ec ? std::format("Could not read source file {}: {}", paths.SourcePath.string(),
                             ec.message())
               : std::format("Source path does not point to a file: {}", paths.SourcePath.string());
        return std::nullopt;
    }
    const auto relative = RelativeCookPath(paths.SourcePath, paths.SourceRoot);
    if (!relative || relative->empty())
    {
        error = std::format("{}: Source texture is outside source root {}", source.string(),
                            paths.SourceRoot.string());
        return std::nullopt;
    }
    if (RelativeCookPath(paths.SourcePath, paths.CookedRoot))
    {
        error =
            std::format("{}: Generated cooked-root files cannot be source inputs", source.string());
        return std::nullopt;
    }
    paths.SourceRelative = *relative;
    std::string name = paths.SourcePath.filename().string();
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    auto stem = paths.SourceRelative;
    if (std::string_view(name).ends_with(".cubemap.json"))
    {
        paths.Kind = TextureCookSourceKind::CubemapDescriptor;
        paths.SettingsPath = paths.SourcePath;
        stem.replace_extension();
    }
    else
    {
        const auto extension = std::filesystem::path(name).extension().string();
        if (extension == ".dds")
            paths.Kind = TextureCookSourceKind::Dds;
        else if (extension != ".png" && extension != ".jpg" && extension != ".jpeg")
        {
            error = std::format("{}: Unsupported source; expected PNG/JPG, DDS or *.cubemap.json",
                                source.string());
            return std::nullopt;
        }
        paths.SettingsPath = paths.SourcePath;
        paths.SettingsPath += ".json";
    }
    paths.CookedPath = paths.CookedRoot / stem;
    paths.CookedPath += ".ktx2";
    paths.RecordPath = paths.CookedRoot / stem;
    paths.RecordPath += ".cook.json";
    for (const auto& [path, root] : {std::pair{paths.SettingsPath, paths.SourceRoot},
                                     std::pair{paths.CookedPath, paths.CookedRoot},
                                     std::pair{paths.RecordPath, paths.CookedRoot}})
    {
        const auto resolved = fs::weakly_canonical(path, ec);
        if (ec || !RelativeCookPath(resolved, root))
        {
            error =
                std::format("{}: Settings/output path escapes its configured root", path.string());
            return std::nullopt;
        }
        if (fs::exists(path, ec) && !fs::is_regular_file(path, ec))
        {
            error = std::format("{}: Settings/output path must be a regular file", path.string());
            return std::nullopt;
        }
        if (ec)
        {
            error = std::format("{}: Cannot inspect settings/output path: {}", path.string(),
                                ec.message());
            return std::nullopt;
        }
    }
    return paths;
}

} // namespace Hikari::Asset
