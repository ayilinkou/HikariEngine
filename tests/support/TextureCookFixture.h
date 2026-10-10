#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <asset/ImageWriter.h>
#include <asset/TextureCooker.h>

class TextureCookFixture
{
public:
    TextureCookFixture()
    {
        std::random_device random;
        for (int attempt = 0; attempt < 16; ++attempt)
        {
            m_Root = std::filesystem::temp_directory_path() /
                     ("hikari cooker test " + std::to_string(random()));
            if (std::filesystem::create_directory(m_Root))
            {
                // The cooker canonicalizes aliases such as Windows short names and symlinks.
                m_Root = std::filesystem::canonical(m_Root);
                std::filesystem::create_directory(Content());
                return;
            }
        }
        throw std::runtime_error("Could not reserve cooker test directory");
    }

    ~TextureCookFixture()
    {
        std::error_code ec;
        std::filesystem::permissions(Content(), std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::add, ec);
        std::filesystem::remove_all(m_Root, ec);
    }
    TextureCookFixture(const TextureCookFixture&) = delete;
    TextureCookFixture& operator=(const TextureCookFixture&) = delete;

    const std::filesystem::path& Root() const { return m_Root; }
    std::filesystem::path Content() const { return m_Root / "content"; }
    std::filesystem::path Cooked() const { return m_Root / "cooked"; }
    std::filesystem::path Source(std::string_view path) const
    {
        return (Content() / path).make_preferred();
    }
    Hikari::Asset::TextureCooker Cooker() const { return Hikari::Asset::TextureCooker(Content()); }

    std::filesystem::path WriteImage(std::string_view name, uint32_t width = 4,
                                     uint32_t height = 4) const
    {
        const auto path = Source(name);
        std::filesystem::create_directories(path.parent_path());
        std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4, 255);
        if (!Hikari::Asset::WritePng(pixels, {width, height}, path.string()))
            throw std::runtime_error("Could not write cooker test image");
        return path;
    }
    static void WriteText(const std::filesystem::path& path, std::string_view text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file << text;
        if (!file)
            throw std::runtime_error("Could not write cooker test text");
    }
    static std::string ReadText(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            throw std::runtime_error("Could not read cooker test text");
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }
    void WriteCubemap(std::string_view settings = "") const
    {
        for (const auto name : {"right", "left", "top", "bottom", "back", "front"})
            WriteImage(std::string("sky/") + name + ".png");
        WriteText(Source("sky/sky.cubemap.json"),
                  std::string(R"({"version":1,)") + std::string(settings) +
                      R"("faces":{"negativeZ":"front.png","positiveY":"top.png",)"
                      R"("positiveX":"right.png","negativeX":"left.png",)"
                      R"("positiveZ":"back.png","negativeY":"bottom.png"}})");
    }

private:
    std::filesystem::path m_Root;
};
