#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include <asset/ImageReader.h>
#include <core/Extent2D.h>

#include "ImageCompare.h"

using namespace Hikari;
using TestSupport::ImageTolerance;

namespace
{
constexpr Core::Extent2D kExtent{4u, 3u};

/** A flat opaque field, so that any difference a test makes is the only one. */
std::vector<uint8_t> SolidImage(Core::Extent2D extent = kExtent, uint8_t value = 100u)
{
    std::vector<uint8_t> pixels(static_cast<size_t>(extent.Width) * extent.Height * 4u, value);
    for (size_t i = 3u; i < pixels.size(); i += 4u)
        pixels[i] = 255u;

    return pixels;
}

/** The byte offset of one channel of the pixel at (x, y). */
size_t At(uint32_t x, uint32_t y, uint32_t channel, Core::Extent2D extent = kExtent)
{
    return (static_cast<size_t>(y) * extent.Width + x) * 4u + channel;
}

/** A unique directory under the system temp dir, removed on destruction. */
class TempDir
{
public:
    TempDir()
    {
        std::random_device rd;
        m_Path = std::filesystem::temp_directory_path() /
                 ("hikari_compare_test_" + std::to_string(rd()));
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(m_Path, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    /** A prefix inside a directory that does not exist yet, on purpose. */
    std::string Prefix() const { return (m_Path / "nested" / "run_").string(); }

    std::string File(std::string_view name) const { return (m_Path / "nested" / name).string(); }

private:
    std::filesystem::path m_Path;
};
} // namespace

TEST_CASE("Identical images compare equal", "[support][image]")
{
    const std::vector<uint8_t> image = SolidImage();

    const TestSupport::ImageComparison result =
        TestSupport::CompareImages(image, kExtent, image, kExtent);

    CHECK(result.bComparable);
    CHECK(result.bWithinTolerance);
    CHECK(result.DifferingPixels == 0u);
    CHECK(result.TotalPixels == 12u);
    CHECK(result.WorstChannelDelta == 0u);
    CHECK(TestSupport::Describe(result) == "identical: 12 pixels, no channel differs");
}

TEST_CASE("One changed pixel fails and names its coordinates", "[support][image]")
{
    const std::vector<uint8_t> expected = SolidImage();
    std::vector<uint8_t> actual = expected;

    // The smallest change representable, at a pixel that is neither the first
    // nor the last: an off-by-one in the row stride lands somewhere else.
    actual[At(2u, 1u, 1u)] += 1u;

    const TestSupport::ImageComparison result =
        TestSupport::CompareImages(actual, kExtent, expected, kExtent);

    REQUIRE(result.bComparable);
    CHECK_FALSE(result.bWithinTolerance);
    CHECK(result.DifferingPixels == 1u);
    CHECK(result.WorstChannelDelta == 1u);
    CHECK(result.WorstPixelX == 2u);
    CHECK(result.WorstPixelY == 1u);
    CHECK(result.Bounds.MinX == 2u);
    CHECK(result.Bounds.MaxX == 2u);
    CHECK(result.Bounds.MinY == 1u);
    CHECK(result.Bounds.MaxY == 1u);
    CHECK(TestSupport::Describe(result).find("(2, 1)") != std::string::npos);
}

TEST_CASE("A difference in alpha alone is caught", "[support][image]")
{
    const std::vector<uint8_t> expected = SolidImage();
    std::vector<uint8_t> actual = expected;
    actual[At(0u, 0u, 3u)] = 254u;

    const TestSupport::ImageComparison result =
        TestSupport::CompareImages(actual, kExtent, expected, kExtent);

    REQUIRE(result.bComparable);
    CHECK_FALSE(result.bWithinTolerance);
    CHECK(result.DifferingPixels == 1u);
}

TEST_CASE("A difference in colour alone is caught where alpha is identical", "[support][image]")
{
    // The converse control, and the one that matters: every capture this engine
    // writes is fully opaque, so a comparison looking only at alpha passes for
    // two completely different pictures. That is the bug the PIL recipe had.
    const std::vector<uint8_t> expected = SolidImage();
    std::vector<uint8_t> actual = SolidImage(kExtent, 200u);

    const TestSupport::ImageComparison result =
        TestSupport::CompareImages(actual, kExtent, expected, kExtent);

    REQUIRE(result.bComparable);
    CHECK_FALSE(result.bWithinTolerance);
    CHECK(result.DifferingPixels == result.TotalPixels);
    CHECK(result.WorstChannelDelta == 100u);
}

TEST_CASE("The bounding box spans every differing pixel", "[support][image]")
{
    const std::vector<uint8_t> expected = SolidImage();
    std::vector<uint8_t> actual = expected;
    actual[At(1u, 0u, 0u)] += 5u;
    actual[At(3u, 2u, 2u)] += 9u;

    const TestSupport::ImageComparison result =
        TestSupport::CompareImages(actual, kExtent, expected, kExtent);

    REQUIRE(result.bComparable);
    CHECK(result.DifferingPixels == 2u);
    CHECK(result.WorstChannelDelta == 9u);
    CHECK(result.WorstPixelX == 3u);
    CHECK(result.WorstPixelY == 2u);
    CHECK(result.Bounds.MinX == 1u);
    CHECK(result.Bounds.MinY == 0u);
    CHECK(result.Bounds.MaxX == 3u);
    CHECK(result.Bounds.MaxY == 2u);
}

TEST_CASE("The two limits catch what the other misses", "[support][image]")
{
    const std::vector<uint8_t> expected = SolidImage();

    SECTION("the ceiling catches one badly wrong pixel that the fraction allows")
    {
        std::vector<uint8_t> actual = expected;
        actual[At(0u, 0u, 0u)] += 40u;

        // One pixel in twelve is over 8%, so allow a tenth of the image to move.
        const ImageTolerance tolerance{.MaxChannelDelta = 8u, .MaxDifferingFraction = 0.1};
        const TestSupport::ImageComparison result =
            TestSupport::CompareImages(actual, kExtent, expected, kExtent, tolerance);

        REQUIRE(result.bComparable);
        CHECK(result.DifferingFraction() <= tolerance.MaxDifferingFraction);
        CHECK_FALSE(result.bWithinTolerance);
    }

    SECTION("the fraction catches a whole image drifting within the ceiling")
    {
        std::vector<uint8_t> actual = expected;
        for (size_t i = 0u; i < actual.size(); i += 4u)
            actual[i] += 1u;

        const ImageTolerance tolerance{.MaxChannelDelta = 8u, .MaxDifferingFraction = 0.1};
        const TestSupport::ImageComparison result =
            TestSupport::CompareImages(actual, kExtent, expected, kExtent, tolerance);

        REQUIRE(result.bComparable);
        CHECK(result.WorstChannelDelta <= tolerance.MaxChannelDelta);
        CHECK_FALSE(result.bWithinTolerance);
    }

    SECTION("a difference inside both limits passes")
    {
        std::vector<uint8_t> actual = expected;
        actual[At(0u, 0u, 0u)] += 3u;

        const ImageTolerance tolerance{.MaxChannelDelta = 8u, .MaxDifferingFraction = 0.1};
        const TestSupport::ImageComparison result =
            TestSupport::CompareImages(actual, kExtent, expected, kExtent, tolerance);

        REQUIRE(result.bComparable);
        CHECK(result.bWithinTolerance);
        CHECK(result.WorstChannelDelta == 3u);
    }
}

TEST_CASE("A zero tolerance rejects the smallest possible difference", "[support][image]")
{
    const std::vector<uint8_t> expected = SolidImage();
    std::vector<uint8_t> actual = expected;
    actual[At(0u, 0u, 0u)] += 1u;

    const TestSupport::ImageComparison result =
        TestSupport::CompareImages(actual, kExtent, expected, kExtent);

    REQUIRE(result.bComparable);
    CHECK_FALSE(result.bWithinTolerance);
}

TEST_CASE("Images that cannot be compared say so", "[support][image]")
{
    const std::vector<uint8_t> image = SolidImage();

    SECTION("different extents")
    {
        const Core::Extent2D other{3u, 4u};
        const std::vector<uint8_t> transposed = SolidImage(other);

        const TestSupport::ImageComparison result =
            TestSupport::CompareImages(image, kExtent, transposed, other);

        CHECK_FALSE(result.bComparable);
        CHECK_FALSE(result.bWithinTolerance);
    }

    SECTION("a buffer that is not four bytes per pixel")
    {
        std::vector<uint8_t> truncated = image;
        truncated.pop_back();

        const TestSupport::ImageComparison result =
            TestSupport::CompareImages(truncated, kExtent, image, kExtent);

        CHECK_FALSE(result.bComparable);
    }

    SECTION("an empty extent")
    {
        const TestSupport::ImageComparison result =
            TestSupport::CompareImages({}, Core::Extent2D{}, {}, Core::Extent2D{});

        CHECK_FALSE(result.bComparable);
    }
}

TEST_CASE("A failure writes actual, expected and an amplified diff", "[support][image]")
{
    const TempDir dir;
    const std::vector<uint8_t> expected = SolidImage();
    std::vector<uint8_t> actual = expected;
    actual[At(2u, 1u, 0u)] += 4u;

    SECTION("at a zero tolerance every differing pixel is fully bright")
    {
        REQUIRE(TestSupport::WriteComparisonImages(actual, kExtent, expected, kExtent,
                                                   ImageTolerance{}, dir.Prefix()));

        const std::optional<Asset::Image> diff = Asset::ReadPng(dir.File("run_diff.png"));
        REQUIRE(diff.has_value());
        CHECK(diff->Extent == kExtent);
        CHECK(diff->Pixels[At(2u, 1u, 0u)] == 255u);
        CHECK(diff->Pixels[At(2u, 1u, 3u)] == 255u);
        CHECK(diff->Pixels[At(0u, 0u, 0u)] == 0u);

        // Round-tripping the inputs too, so that a diff pointing at the right
        // pixel of the wrong image would still fail.
        const std::optional<Asset::Image> writtenActual =
            Asset::ReadPng(dir.File("run_actual.png"));
        REQUIRE(writtenActual.has_value());
        CHECK(writtenActual->Pixels == actual);

        const std::optional<Asset::Image> writtenExpected =
            Asset::ReadPng(dir.File("run_expected.png"));
        REQUIRE(writtenExpected.has_value());
        CHECK(writtenExpected->Pixels == expected);
    }

    SECTION("a non-zero ceiling maps onto full scale")
    {
        // A delta of 4 against a ceiling of 8 is half brightness.
        const ImageTolerance tolerance{.MaxChannelDelta = 8u};
        REQUIRE(TestSupport::WriteComparisonImages(actual, kExtent, expected, kExtent, tolerance,
                                                   dir.Prefix()));

        const std::optional<Asset::Image> diff = Asset::ReadPng(dir.File("run_diff.png"));
        REQUIRE(diff.has_value());
        CHECK(diff->Pixels[At(2u, 1u, 0u)] == 128u);
        CHECK(diff->Pixels[At(0u, 0u, 0u)] == 0u);
    }
}

TEST_CASE("Mismatched extents write both images and no diff", "[support][image]")
{
    const TempDir dir;
    const Core::Extent2D other{3u, 4u};

    REQUIRE(TestSupport::WriteComparisonImages(SolidImage(), kExtent, SolidImage(other), other,
                                               ImageTolerance{}, dir.Prefix()));

    CHECK(std::filesystem::exists(dir.File("run_actual.png")));
    CHECK(std::filesystem::exists(dir.File("run_expected.png")));
    CHECK_FALSE(std::filesystem::exists(dir.File("run_diff.png")));
}
