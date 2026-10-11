#include "CookFileUtil.h"

#include <format>
#include <fstream>
#include <iterator>
#include <random>
#include <system_error>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

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

bool ResolveCookRoots(const std::filesystem::path& source, const std::filesystem::path& cooked,
                      std::filesystem::path& resolvedSource, std::filesystem::path& resolvedCooked,
                      std::string& error)
{
    namespace fs = std::filesystem;
    error.clear();
    std::error_code ec;
    if (source.empty() || !fs::is_directory(source, ec))
    {
        error = std::format("{}: Source root must be an existing directory; pass --source-root",
                            source.string());
        return false;
    }
    resolvedSource = fs::canonical(source, ec);
    if (ec)
    {
        error = std::format("{}: Cannot resolve source root: {}", source.string(), ec.message());
        return false;
    }
    resolvedCooked = fs::weakly_canonical(
        cooked.empty() ? resolvedSource.parent_path() / "cooked" : fs::absolute(cooked, ec), ec);
    if (ec || resolvedCooked.empty())
    {
        error = std::format("{}: Cannot resolve cooked root: {}", cooked.string(), ec.message());
        return false;
    }
    if (fs::exists(resolvedCooked, ec) && !fs::is_directory(resolvedCooked, ec))
    {
        error = std::format("{}: Cooked root is not a directory; choose another --cooked-root",
                            resolvedCooked.string());
        return false;
    }
    if (ec)
    {
        error = std::format("{}: Cannot inspect cooked root: {}. Choose another --cooked-root.",
                            resolvedCooked.string(), ec.message());
        return false;
    }
    return true;
}

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
            bPublished = FlushCookFile(temporary, error) && ReplaceCookFile(temporary, path, error);
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

bool FlushCookFile(const std::filesystem::path& path, std::string& error)
{
#ifdef _WIN32
    const auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        error = std::format("{}: Cannot open file for flushing (Windows error {})", path.string(),
                            GetLastError());
        return false;
    }
    const bool success = FlushFileBuffers(handle) != 0;
    const auto code = GetLastError();
    CloseHandle(handle);
#else
    const int handle = open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (handle < 0)
    {
        error = std::format("{}: Cannot open file for flushing: {}", path.string(),
                            std::error_code(errno, std::generic_category()).message());
        return false;
    }
    int status;
    do
        status = fsync(handle);
    while (status < 0 && errno == EINTR);
    const bool success = status == 0;
    const int code = errno;
    close(handle);
#endif
    if (!success)
        error =
            std::format("{}: Cannot flush file: {}", path.string(),
                        std::error_code(static_cast<int>(code), std::system_category()).message());
    return success;
}

bool ReplaceCookFile(const std::filesystem::path& temporary,
                     const std::filesystem::path& destination, std::string& error)
{
    std::error_code ec;
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#else
    std::filesystem::rename(temporary, destination, ec);
#endif
    if (ec)
    {
        error = std::format("{}: Cannot atomically replace file: {}", destination.string(),
                            ec.message());
        return false;
    }
    return true;
}

} // namespace Hikari::Asset
