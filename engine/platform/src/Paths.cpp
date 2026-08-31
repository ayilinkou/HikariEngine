#include <platform/Paths.h>

#include <format>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

#include <core/Log.h>

namespace Hikari::Platform
{

namespace
{
constexpr Core::LogCategory LogPaths("Paths");

constexpr const char* kContentEnvVar = "HIKARI_CONTENT";
constexpr const char* kContentDirName = "content";
constexpr const char* kShaderDirName = "shaders";

constexpr const char* kUserDataEnvVar = "HIKARI_USER_DATA";
constexpr const char* kAppName = "HikariEngine";

bool IsDirectory(const std::filesystem::path& path)
{
    // The error_code overload so a permission failure or a broken symlink
    // reports "not a directory" rather than throwing.
    std::error_code ec;
    return !path.empty() && std::filesystem::is_directory(path, ec);
}

/**
 * Creates `path` if it is not already a directory. Reports failure rather than
 * throwing: a missing place to write caches is a degraded run, not a broken one.
 */
bool EnsureDirectory(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return IsDirectory(path);
}

/**
 * The directory the platform sets aside for this application's own files, or
 * empty if it will not give one.
 */
std::filesystem::path ResolveUserDataRoot()
{
    // Unlike the content root, an explicit override is created rather than
    // required to exist: nothing ships here, so an empty directory is the
    // correct starting state and demanding one be made first is ceremony.
    if (const char* environmentRoot = SDL_getenv(kUserDataEnvVar))
    {
        if (EnsureDirectory(environmentRoot))
            return environmentRoot;

        Core::LogMsg(Core::LogSeverity::Warning, LogPaths,
                     "{} is set to \"{}\", which could not be created. Nothing will be written to "
                     "the user data directory this run.",
                     kUserDataEnvVar, environmentRoot);
        return {};
    }

    // SDL_GetPrefPath() creates the directory and returns it with a trailing
    // separator: $XDG_DATA_HOME/HikariEngine on Linux, %APPDATA%\HikariEngine
    // on Windows.
    //
    // The organisation argument is empty because this project has no
    // organisation name to give, and passing the application name for both
    // would nest it under itself. SDL warns that omitting it can fail app store
    // certification, which is a packaging decision for whenever there is
    // something to package.
    char* prefPath = SDL_GetPrefPath("", kAppName);
    if (!prefPath)
    {
        Core::LogMsg(
            Core::LogSeverity::Warning, LogPaths,
            "No user data directory available ({}). Nothing will be written to it this run.",
            SDL_GetError());
        return {};
    }

    const std::filesystem::path root(prefPath);
    SDL_free(prefPath);
    return root;
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
    // terminated with a separator.
    if (const char* basePath = SDL_GetBasePath())
    {
        candidates.ExecutableRelative = std::filesystem::path(basePath) / kContentDirName;

        // The shader root is not resolved from candidates the way the content
        // root is, because there is nothing to choose between: the build puts
        // the SPIR-V here and only a binary built alongside it can use it. A
        // missing directory is therefore a broken build rather than a
        // misconfiguration, and it surfaces where the shader is loaded, naming
        // the file that could not be opened.
        m_ShaderRoot = std::filesystem::path(basePath) / kShaderDirName;
    }

    // Last resort, so that a freshly built tree runs before anything has been
    // installed next to the executable.
    candidates.SourceRelative = std::filesystem::path(HIKARI_SOURCE_DIR) / kContentDirName;

    m_ContentRoot = ResolveContentRoot(candidates);

    Core::LogMsg(Core::LogSeverity::Info, LogPaths, "Content root: {}", m_ContentRoot.string());
    Core::LogMsg(Core::LogSeverity::Info, LogPaths, "Shader root: {}", m_ShaderRoot.string());

    m_UserDataRoot = ResolveUserDataRoot();
    if (!m_UserDataRoot.empty())
        Core::LogMsg(Core::LogSeverity::Info, LogPaths, "User data root: {}",
                     m_UserDataRoot.string());
}

std::filesystem::path Paths::Content(std::string_view relativePath) const
{
    const std::filesystem::path path(relativePath);

    // An absolute path is already fully specified — a scene passed on the
    // command line or picked from a file dialog must be used as given, not
    // re-rooted under the content directory.
    return path.is_absolute() ? path : m_ContentRoot / path;
}

std::filesystem::path Paths::Shader(std::string_view relativePath) const
{
    const std::filesystem::path path(relativePath);

    return path.is_absolute() ? path : m_ShaderRoot / path;
}

std::filesystem::path Paths::UserData(std::string_view relativePath) const
{
    if (m_UserDataRoot.empty())
        return {};

    return m_UserDataRoot / std::filesystem::path(relativePath);
}
} // namespace Hikari::Platform
