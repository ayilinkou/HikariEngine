#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>

#include <platform/Paths.h>

namespace
{
// A unique directory under the system temp dir, removed with everything under
// it on destruction. Content-root resolution is defined in terms of "does this
// directory exist", so the tests need real directories rather than fakes.
class TempDir
{
public:
    TempDir()
    {
        std::random_device rd;
        m_Path = std::filesystem::temp_directory_path() /
                 ("vulkanapp_paths_test_" + std::to_string(rd()));
        std::filesystem::create_directories(m_Path);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(m_Path, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    std::filesystem::path MakeDir(std::string_view name) const
    {
        const std::filesystem::path path = m_Path / name;
        std::filesystem::create_directories(path);
        return path;
    }

    std::filesystem::path MakeFile(std::string_view name) const
    {
        const std::filesystem::path path = m_Path / name;
        std::ofstream file(path);
        file << "not a directory";
        return path;
    }

    // A path under the temp dir that is guaranteed not to exist.
    std::filesystem::path Missing(std::string_view name) const { return m_Path / name; }

private:
    std::filesystem::path m_Path;
};
} // namespace

TEST_CASE("The command line override wins over every other candidate", "[Paths]")
{
    const TempDir temp;
    ContentRootCandidates candidates{.CommandLineOverride = temp.MakeDir("cli"),
                                     .EnvironmentOverride = temp.MakeDir("env"),
                                     .ExecutableRelative = temp.MakeDir("exe"),
                                     .SourceRelative = temp.MakeDir("source")};

    REQUIRE(ResolveContentRoot(candidates) == candidates.CommandLineOverride);
}

TEST_CASE("The environment override wins when no command line override is given", "[Paths]")
{
    const TempDir temp;
    ContentRootCandidates candidates{.EnvironmentOverride = temp.MakeDir("env"),
                                     .ExecutableRelative = temp.MakeDir("exe"),
                                     .SourceRelative = temp.MakeDir("source")};

    REQUIRE(ResolveContentRoot(candidates) == candidates.EnvironmentOverride);
}

TEST_CASE("The executable-relative root wins over the source-relative one", "[Paths]")
{
    const TempDir temp;
    ContentRootCandidates candidates{.ExecutableRelative = temp.MakeDir("exe"),
                                     .SourceRelative = temp.MakeDir("source")};

    REQUIRE(ResolveContentRoot(candidates) == candidates.ExecutableRelative);
}

TEST_CASE("The source-relative root is the last resort", "[Paths]")
{
    const TempDir temp;
    ContentRootCandidates candidates{.SourceRelative = temp.MakeDir("source")};

    REQUIRE(ResolveContentRoot(candidates) == candidates.SourceRelative);
}

TEST_CASE("A non-existent implicit candidate is skipped rather than returned", "[Paths]")
{
    const TempDir temp;
    ContentRootCandidates candidates{.ExecutableRelative = temp.Missing("exe"),
                                     .SourceRelative = temp.MakeDir("source")};

    REQUIRE(ResolveContentRoot(candidates) == candidates.SourceRelative);
}

TEST_CASE("A command line override that does not exist is an error, not a fallback", "[Paths]")
{
    const TempDir temp;
    ContentRootCandidates candidates{.CommandLineOverride = temp.Missing("typo"),
                                     .SourceRelative = temp.MakeDir("source")};

    REQUIRE_THROWS_AS(ResolveContentRoot(candidates), ContentRootError);
}

TEST_CASE("An environment override that does not exist is an error, not a fallback", "[Paths]")
{
    const TempDir temp;
    ContentRootCandidates candidates{.EnvironmentOverride = temp.Missing("typo"),
                                     .SourceRelative = temp.MakeDir("source")};

    REQUIRE_THROWS_AS(ResolveContentRoot(candidates), ContentRootError);
}

TEST_CASE("A candidate naming a file rather than a directory is not a content root", "[Paths]")
{
    const TempDir temp;

    SECTION("explicit override throws")
    {
        ContentRootCandidates candidates{.CommandLineOverride = temp.MakeFile("file.txt"),
                                         .SourceRelative = temp.MakeDir("source")};

        REQUIRE_THROWS_AS(ResolveContentRoot(candidates), ContentRootError);
    }

    SECTION("implicit candidate is skipped")
    {
        ContentRootCandidates candidates{.ExecutableRelative = temp.MakeFile("file.txt"),
                                         .SourceRelative = temp.MakeDir("source")};

        REQUIRE(ResolveContentRoot(candidates) == candidates.SourceRelative);
    }
}

TEST_CASE("Resolution fails when no candidate exists", "[Paths]")
{
    const TempDir temp;
    ContentRootCandidates candidates{.ExecutableRelative = temp.Missing("exe"),
                                     .SourceRelative = temp.Missing("source")};

    REQUIRE_THROWS_AS(ResolveContentRoot(candidates), ContentRootError);
}

TEST_CASE("Content() joins a relative path onto the resolved root", "[Paths]")
{
    const TempDir temp;
    const std::filesystem::path root = temp.MakeDir("content");

    // Passing the root as the command line override keeps this independent of
    // where the test binary happens to live.
    const Paths paths{root.string()};

    REQUIRE(paths.ContentRoot() == root);
    REQUIRE(paths.Content("shaders/opaque.spv") == root / "shaders/opaque.spv");
}
