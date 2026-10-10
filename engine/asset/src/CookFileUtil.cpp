#include "CookFileUtil.h"

#include <format>
#include <fstream>
#include <iterator>
#include <random>
#include <system_error>
#include <vector>

namespace Hikari::Asset
{

namespace
{
std::optional<std::filesystem::path> CreateScratchDirectory(const std::filesystem::path& parent,
                                                            std::string& error)
{
    std::random_device random;
    for (int attempt = 0; attempt < 16; ++attempt)
    {
        const auto path = parent / std::format(".hikari-cook-{:08x}-{:08x}", random(), random());
        std::error_code ec;
        // Directory creation is exclusive, unlike truncating a random temp file.
        if (std::filesystem::create_directory(path, ec))
            return path;
        if (ec)
        {
            error = std::format("{}: Cannot create cooking scratch directory: {}", parent.string(),
                                ec.message());
            return std::nullopt;
        }
    }
    error =
        std::format("{}: Could not reserve a unique cooking scratch directory", parent.string());
    return std::nullopt;
}
} // namespace

std::optional<std::filesystem::path> RelativeCookPath(const std::filesystem::path& path,
                                                      const std::filesystem::path& root)
{
    std::vector<std::filesystem::path> reverse;
    auto current = path;
    while (!current.empty())
    {
        std::error_code ec;
        if (current == root || std::filesystem::equivalent(current, root, ec))
        {
            std::filesystem::path relative;
            for (auto it = reverse.rbegin(); it != reverse.rend(); ++it)
                relative /= *it;
            return relative;
        }
        const auto parent = current.parent_path();
        if (current == parent)
            break;
        reverse.push_back(current.filename());
        current = parent;
    }
    return std::nullopt;
}

std::optional<std::string> ReadCookText(const std::filesystem::path& path, std::string& error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        error = std::format("{}: Cannot read settings file", path.string());
        return std::nullopt;
    }
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (file.bad())
    {
        error = std::format("{}: Failed while reading settings file", path.string());
        return std::nullopt;
    }
    return text;
}

bool WriteCookText(const std::filesystem::path& path, std::string_view text, std::string& error)
{
    const auto directory = CreateScratchDirectory(path.parent_path(), error);
    if (!directory)
        return false;
    const auto temporary = *directory / "settings.json";
    bool bPublished = false;
    {
        std::ofstream file(temporary, std::ios::binary);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.close();
        if (!file)
            error = std::format("{}: Failed to write settings; previous settings were retained",
                                path.string());
        else
        {
            std::error_code ec;
            // std::filesystem::rename replaces an existing file with POSIX
            // rename semantics, including on MSVC; never truncate authored JSON.
            std::filesystem::rename(temporary, path, ec);
            bPublished = !ec;
            if (ec)
                error = std::format("{}: Cannot replace settings: {}", path.string(), ec.message());
        }
    }
    std::error_code ignored;
    std::filesystem::remove_all(*directory, ignored);
    return bPublished;
}

bool CheckCookOutputDirectory(const std::filesystem::path& directory, std::string& error)
{
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec)
    {
        error = std::format(
            "{}: Cannot create cooked output directory: {}. Choose a writable --cooked-root.",
            directory.string(), ec.message());
        return false;
    }
    const auto scratch = CreateScratchDirectory(directory, error);
    if (!scratch)
    {
        error += " Choose a writable --cooked-root.";
        return false;
    }
    std::filesystem::remove(*scratch, ec);
    if (ec)
    {
        error = std::format("{}: Cannot remove cooking scratch directory: {}", scratch->string(),
                            ec.message());
        return false;
    }
    return true;
}

} // namespace Hikari::Asset
