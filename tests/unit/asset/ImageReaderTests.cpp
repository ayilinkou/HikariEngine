#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include <asset/ImageReader.h>
#include <asset/ImageWriter.h>
#include <core/Extent2D.h>

using namespace Hikari;

namespace
{
/**
 * A unique directory under the system temp dir, removed with everything under
 * it on destruction. Encoding and decoding both go through the filesystem, so
 * the round trip needs real files rather than buffers.
 */
class TempDir
{
public:
    TempDir()
    {
        std::random_device rd;
        m_Path =
            std::filesystem::temp_directory_path() / ("hikari_image_test_" + std::to_string(rd()));
        std::filesystem::create_directories(m_Path);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(m_Path, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    std::string File(std::string_view name) const { return (m_Path / name).string(); }

private:
    std::filesystem::path m_Path;
};

/**
 * Pixels whose every byte is distinct, so a channel swap, a row stride error
 * and a transposed extent each produce a different wrong answer rather than the
 * same one. The alpha channel varies too: a capture is opaque, and a decoder
 * that dropped alpha would otherwise round-trip unnoticed.
 */
std::vector<uint8_t> DistinctPixels(Core::Extent2D extent)
{
    std::vector<uint8_t> pixels(static_cast<size_t>(extent.Width) * extent.Height * 4u);
    for (size_t i = 0; i < pixels.size(); ++i)
        pixels[i] = static_cast<uint8_t>(i * 7u + 1u);

    return pixels;
}
} // namespace

TEST_CASE("ReadPng returns what WritePng wrote", "[asset][image]")
{
    const TempDir dir;
    const std::string path = dir.File("roundtrip.png");
    const Core::Extent2D extent{5u, 3u};
    const std::vector<uint8_t> written = DistinctPixels(extent);

    REQUIRE(Asset::WritePng(written, extent, path));

    const std::optional<Asset::Image> read = Asset::ReadPng(path);
    REQUIRE(read.has_value());
    CHECK(read->Extent == extent);
    CHECK(read->Pixels == written);
}

TEST_CASE("ReadPng keeps width and height apart", "[asset][image]")
{
    const TempDir dir;
    const std::string path = dir.File("oblong.png");

    // Deliberately not square: a transposed extent survives a square image.
    const Core::Extent2D extent{1u, 4u};
    const std::vector<uint8_t> written = DistinctPixels(extent);

    REQUIRE(Asset::WritePng(written, extent, path));

    const std::optional<Asset::Image> read = Asset::ReadPng(path);
    REQUIRE(read.has_value());
    CHECK(read->Extent.Width == 1u);
    CHECK(read->Extent.Height == 4u);
    CHECK(read->Pixels == written);
}

TEST_CASE("ReadPng reports a file it cannot decode", "[asset][image]")
{
    const TempDir dir;

    SECTION("a path that does not exist")
    {
        CHECK_FALSE(Asset::ReadPng(dir.File("absent.png")).has_value());
    }

    SECTION("a file that is not an image")
    {
        const std::string path = dir.File("text.png");
        std::ofstream(path) << "this is not a PNG";

        CHECK_FALSE(Asset::ReadPng(path).has_value());
    }
}
