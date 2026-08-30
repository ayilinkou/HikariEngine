#pragma once

#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Hikari::Platform
{

inline std::vector<char> ReadFile(const std::string filename)
{
    // std::ios::ate starts to read at end of file so that we can get the size
    // of the buffer
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error(std::format("Failed to open file: {}", filename));

    std::vector<char> buffer(file.tellg());
    file.seekg(0, std::ios::beg);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    file.close();

    return buffer;
}

// Creates the directories leading up to `path`, so that writing to it succeeds
// even on a first run. Does nothing if `path` has no parent.
inline void EnsureParentDirectoryExists(std::string_view path)
{
    std::filesystem::path p(path);
    if (p.has_parent_path())
    {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec)
        {
            throw std::runtime_error(std::format("Failed to create directory {}: {}",
                                                 p.parent_path().string(), ec.message()));
        }
    }
}

// Forces `path` to end in `ext`, replacing any extension already there.
inline std::string EnsureExtension(const std::string& path, const std::string& ext)
{
    std::filesystem::path p(path);
    if (p.extension() != ext)
        p.replace_extension(ext);
    return p.string();
}
} // namespace Hikari::Platform
