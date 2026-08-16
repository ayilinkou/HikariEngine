#include <platform/Paths.h>

#include <format>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <core/Log.h>

namespace
{
constexpr LogCategory LogPaths("Paths");

constexpr const char* kContentEnvVar = "VULKANAPP_CONTENT";
constexpr const char* kContentDirName = "content";

bool IsDirectory(const std::filesystem::path& path)
{
    // The error_code overload so a permission failure or a broken symlink
    // reports "not a directory" rather than throwing.
    std::error_code ec;
    return !path.empty() && std::filesystem::is_directory(path, ec);
}
} // namespace

std::filesystem::path ResolveContentRoot(const ContentRootCandidates& candidates)
{
    const auto wasRequestedExplicitly = [](const std::filesystem::path& path, const char* source)
    {
        if (path.empty())
            return false;

        if (!IsDirectory(path))
            throw ContentRootError(std::format("{} is set to \"{}\", which is not a directory",
                                               source, path.string()));

        return true;
    };

    if (wasRequestedExplicitly(candidates.CommandLineOverride, "--content"))
        return candidates.CommandLineOverride;

    if (wasRequestedExplicitly(candidates.EnvironmentOverride, kContentEnvVar))
        return candidates.EnvironmentOverride;

    if (IsDirectory(candidates.ExecutableRelative))
        return candidates.ExecutableRelative;

    if (IsDirectory(candidates.SourceRelative))
        return candidates.SourceRelative;

    throw ContentRootError(
        std::format("No content root found. Tried \"{}\" and \"{}\". Pass --content <dir> or set "
                    "{}.",
                    candidates.ExecutableRelative.string(), candidates.SourceRelative.string(),
                    kContentEnvVar));
}

Paths::Paths(std::string_view commandLineOverride)
{
    ContentRootCandidates candidates;
    candidates.CommandLineOverride = commandLineOverride;

    if (const char* environmentRoot = SDL_getenv(kContentEnvVar))
        candidates.EnvironmentOverride = environmentRoot;

    // SDL_GetBasePath() is the directory the executable lives in, already
    // terminated with a separator. On macOS it resolves to the .app bundle's
    // Resources directory when bundled, which is where content belongs there.
    if (const char* basePath = SDL_GetBasePath())
        candidates.ExecutableRelative = std::filesystem::path(basePath) / kContentDirName;

    // Last resort, so that a freshly built tree runs before anything has been
    // installed next to the executable.
    candidates.SourceRelative = std::filesystem::path(VULKANAPP_SOURCE_DIR) / kContentDirName;

    m_ContentRoot = ResolveContentRoot(candidates);

    LogMsg(LogSeverity::Info, LogPaths, "Content root: {}", m_ContentRoot.string());
}

std::filesystem::path Paths::Content(std::string_view relativePath) const
{
    const std::filesystem::path path(relativePath);

    // An absolute path is already fully specified — a scene passed on the
    // command line or picked from a file dialog must be used as given, not
    // re-rooted under the content directory.
    return path.is_absolute() ? path : m_ContentRoot / path;
}
