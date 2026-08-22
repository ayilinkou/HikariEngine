#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

// The places a content root can come from, in the order they are tried. Empty
// entries are skipped.
struct ContentRootCandidates
{
    std::filesystem::path CommandLineOverride{}; // --content
    std::filesystem::path EnvironmentOverride{}; // VULKANAPP_CONTENT
    std::filesystem::path ExecutableRelative{};  // <exe dir>/content
    std::filesystem::path SourceRelative{};      // <source dir>/content
};

class ContentRootError : public std::runtime_error
{
public:
    explicit ContentRootError(const std::string& message) : std::runtime_error(message) {}
};

// Returns the first candidate that names an existing directory.
//
// A root that was asked for explicitly — on the command line or through the
// environment — is never allowed to fall through to a lower-priority
// candidate: a mistyped `--content` must fail loudly rather than quietly load
// whatever the next candidate happens to contain. Throws ContentRootError in
// that case, and also when no candidate resolves at all.
std::filesystem::path ResolveContentRoot(const ContentRootCandidates& candidates);

// Resolves the content root once at construction, then answers path queries
// against it. Constructing this is what makes asset paths independent of the
// current working directory.
class Paths
{
public:
    // `commandLineOverride` is the value of --content, or empty if not given.
    explicit Paths(std::string_view commandLineOverride = {});

    const std::filesystem::path& ContentRoot() const { return m_ContentRoot; }

    // Content("shaders/opaque.spv") -> <root>/shaders/opaque.spv.
    // An absolute path is returned unchanged.
    std::filesystem::path Content(std::string_view relativePath) const;

    // The per-user directory this application may write to: caches, settings,
    // saves. Distinct from the content root, which ships with the application
    // and is read-only on every platform that installs it properly.
    //
    // Empty when the platform would not give one. That is deliberately not an
    // error: everything written here can be regenerated, so a caller that
    // cannot have a directory skips writing rather than failing the run.
    const std::filesystem::path& UserDataRoot() const { return m_UserDataRoot; }

    // UserData("pipeline_cache.bin") -> <user data root>/pipeline_cache.bin,
    // or an empty path when there is no user data root — so "nowhere to write"
    // and "nothing to read" reach the caller as the same answer.
    std::filesystem::path UserData(std::string_view relativePath) const;

private:
    std::filesystem::path m_ContentRoot;
    std::filesystem::path m_UserDataRoot;
};
